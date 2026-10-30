#!/usr/bin/env python3
"""Static analyser for the Falcon HP application partition.

The shipped image cannot be analysed by REA/Hopper string discovery: Hopper
creates no string objects for constants embedded in the executable section,
and the vendor links every string constant into the code region. This tool
therefore performs its own Thumb-2 flow analysis of the flat image and
recovers what the disassembly engine cannot: literal pools, string
references, the call graph, per-function RAM/peripheral usage and peripheral
attribution.

Example:
    tools/analyze.py --hex Falcon-HP-144pin-8M-Mark-261001.hex strings
    tools/analyze.py --hex ... xrefs 0x08016048
    tools/analyze.py --hex ... func 0x08015dfc
    tools/analyze.py --hex ... census
"""

import argparse
import json
import re
import struct
import sys
from collections import defaultdict

try:
    from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
    from capstone.arm import ARM_CC_AL, ARM_INS_ADR, ARM_INS_B, ARM_INS_BL
    from capstone.arm import ARM_INS_BLX, ARM_INS_CBNZ, ARM_INS_CBZ
    from capstone.arm import ARM_REG_LR, ARM_REG_PC, ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG
except ImportError:
    sys.exit("capstone is required: python3 -m venv /tmp/opencode/.venv && "
             "/tmp/opencode/.venv/bin/pip install capstone==5.0.7")

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from hex2elf import load_ihex

PERIPHERAL_BASES = {
    0x40000400: "TIM3", 0x40000800: "TIM4", 0x40000C00: "TIM5",
    0x40001000: "TIM6", 0x40001400: "TIM7", 0x40002800: "RTC",
    0x40002C00: "WWDG", 0x40003000: "IWDG", 0x40003800: "SPI2",
    0x40003C00: "SPI3", 0x40004400: "USART2", 0x40004800: "USART3",
    0x40004C00: "UART4", 0x40005000: "UART5", 0x40005400: "I2C1",
    0x40005800: "I2C2", 0x40005C00: "USB/CAN", 0x40006400: "CAN",
    0x40006C00: "BKP", 0x40007000: "PWR", 0x40007400: "DAC",
    0x40010000: "AFIO", 0x40010400: "EXTI/TIM8", 0x40010800: "GPIOA",
    0x40010C00: "GPIOB", 0x40011000: "GPIOC", 0x40011400: "GPIOD",
    0x40011800: "GPIOE", 0x40011C00: "GPIOF", 0x40012000: "GPIOG",
    0x40012400: "ADC1 (F1 addr)", 0x40012800: "ADC2/ADC3", 0x40013000: "SPI1",
    0x40013800: "USART1", 0x40013C00: "ADC (AT32 addr)", 0x40014400: "TIM10",
    0x40014C00: "TIM11", 0x40020000: "DMA1", 0x40020400: "DMA2",
    0x40021000: "RCC", 0x40022000: "FLASH", 0x40023000: "CRC",
    0xA0000000: "FSMC", 0xE000E010: "SysTick", 0xE000E100: "NVIC",
    0xE000ED00: "SCB", 0xE0042000: "DBGMCU",
}

PERIPHERAL_WINDOW = 0x1000

RAM_LO = 0x20000000
RAM_HI = 0x20020000
FLASH_LO = 0x08000000
FLASH_HI = 0x08100000


class Function:
    def __init__(self, entry):
        self.entry = entry
        self.insns = []
        self.callers = set()
        self.callees = set()
        self.strings = {}
        self.literals = {}
        self.ram_refs = set()
        self.peripherals = {}
        self.terminated = False

    def to_dict(self):
        return {
            "entry": hex(self.entry),
            "instruction_count": len(self.insns),
            "callers": sorted(hex(x) for x in self.callers),
            "callees": sorted(hex(x) for x in self.callees),
            "strings": {hex(k): v for k, v in sorted(self.strings.items())},
            "literals": {hex(k): hex(v) for k, v in sorted(self.literals.items())},
            "ram_refs": sorted(hex(x) for x in self.ram_refs),
            "peripherals": {k: sorted(hex(x) for x in v)
                            for k, v in sorted(self.peripherals.items())},
        }


class Analyzer:
    def __init__(self, blob, base):
        self.blob = blob
        self.base = base
        self.md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        self.md.detail = True
        self.owner = {}
        self.functions = {}
        self.prologues = set()
        self.strings = {}
        self.string_index = {}
        self.peripheral_totals = defaultdict(set)
        self.literal_totals = defaultdict(set)

    def word(self, addr):
        off = addr - self.base
        if off < 0 or off + 4 > len(self.blob):
            return None
        return struct.unpack_from("<I", self.blob, off)[0]

    def byte(self, addr):
        off = addr - self.base
        if off < 0 or off >= len(self.blob):
            return None
        return self.blob[off]

    def decode(self, addr):
        off = addr - self.base
        if off < 0 or off + 4 > len(self.blob):
            return None
        for insn in self.md.disasm(self.blob[off:off + 4], addr, count=1):
            return insn
        return None

    def extract_strings(self, min_len=4):
        blob = self.blob
        base = self.base
        start = None
        buf = bytearray()
        for i, b in enumerate(blob):
            addr = base + i
            if 0x20 <= b < 0x7F:
                if start is None:
                    start = addr
                buf.append(b)
                continue
            if start is not None and len(buf) >= min_len:
                self._add_string(start, bytes(buf).decode("ascii"))
            start = None
            buf = bytearray()
        if start is not None and len(buf) >= min_len:
            self._add_string(start, bytes(buf).decode("ascii"))

    def _add_string(self, start, text):
        if start in self.strings:
            return
        self.strings[start] = text
        for i in range(len(text)):
            self.string_index[start + i] = (start, text)

    def raw_string(self, addr, length):
        """Return exactly `length` printable characters at addr, or None."""
        off = addr - self.base
        if off < 0 or off + length > len(self.blob):
            return None
        chunk = self.blob[off:off + length]
        if not all(0x20 <= c < 0x7F for c in chunk):
            return None
        return chunk.decode('ascii')

    def string_at(self, addr):
        hit = self.string_index.get(addr)
        if hit is None:
            return None
        return hit

    def classify_constant(self, value):
        if RAM_LO <= value < RAM_HI:
            return "ram"
        if FLASH_LO <= value < FLASH_HI:
            return "flash"
        return None

    def attribute_peripheral(self, value):
        best = None
        for base, name in PERIPHERAL_BASES.items():
            if base <= value < base + PERIPHERAL_WINDOW:
                if best is None or base > best[0]:
                    best = (base, name)
        if best is None:
            return None
        return "%s+0x%X" % (best[1], value - best[0])

    def roots(self):
        roots = set()
        table = self.base
        for i in range(84):
            v = self.word(table + 4 * i)
            if v and FLASH_LO <= v < FLASH_HI:
                roots.add(v & ~1)
        blob = self.blob
        for i in range(0, len(blob) - 3, 2):
            hw = blob[i] | (blob[i + 1] << 8)
            if (hw & 0xFF00) == 0xB500 and (hw & 0x00FF):
                roots.add(self.base + i)
                self.prologues.add(self.base + i)
            if (hw & 0xFE00) == 0xE800 and (hw & 0x1FF) == 0x02D:
                roots.add(self.base + i)
                self.prologues.add(self.base + i)
        for off in range(0, len(self.blob) - 3, 4):
            value = struct.unpack_from("<I", self.blob, off)[0]
            target = value & ~1
            if value & 1 and FLASH_LO <= target < FLASH_HI:
                roots.add(target)
        return sorted(roots)

    def analyze(self):
        self.extract_strings()
        pending = list(self.roots())
        while pending:
            entry = pending.pop()
            if entry in self.functions:
                continue
            self.functions[entry] = Function(entry)
            self.traverse(entry, self.functions[entry], pending)
        for func in self.functions.values():
            for callee in func.callees:
                target = self.functions.get(callee)
                if target is not None:
                    target.callers.add(func.entry)
        return self

    def traverse(self, entry, func, pending):
        stack = [entry]
        while stack:
            addr = stack.pop()
            while True:
                if addr in self.owner or addr < self.base or addr >= self.base + len(self.blob):
                    break
                insn = self.decode(addr)
                if insn is None:
                    self.owner[addr] = func
                    addr += 2
                    continue
                self.owner[addr] = func
                func.insns.append(insn)
                self.record_insn(func, insn)
                kind = insn.id
                if kind in (ARM_INS_BL, ARM_INS_BLX):
                    target = self.branch_target(insn)
                    if target is not None:
                        func.callees.add(target)
                        if target not in self.owner:
                            if target in self.functions:
                                self.functions[target].callers.add(func.entry)
                            else:
                                pending.append(target)
                    addr += insn.size
                    continue
                if kind == ARM_INS_B:
                    target = self.branch_target(insn)
                    conditional = insn.cc != ARM_CC_AL
                    if target is not None and target not in self.owner:
                        if conditional:
                            stack.append(target)
                        elif target in self.prologues and target != func.entry:
                            pending.append(target)
                        else:
                            stack.append(target)
                    if not conditional:
                        func.terminated = True
                        break
                if kind in (ARM_INS_CBZ, ARM_INS_CBNZ):
                    target = self.branch_target(insn)
                    if target is not None and target not in self.owner:
                        stack.append(target)
                if kind == ARM_INS_ADR or self.is_pc_relative(insn):
                    self.record_pc_relative(func, insn)
                if self.is_return(insn):
                    func.terminated = True
                    break
                addr += insn.size

    def branch_target(self, insn):
        if not insn.operands:
            return None
        op = insn.operands[0]
        if op.type == ARM_OP_IMM:
            return op.imm & ~1
        return None

    def is_pc_relative(self, insn):
        if not insn.operands or len(insn.operands) < 3:
            return False
        if insn.mnemonic not in ("add", "addw", "sub", "subw", "adr"):
            return False
        src = insn.operands[1]
        return src.type == ARM_OP_REG and src.reg == ARM_REG_PC

    def is_return(self, insn):
        if insn.mnemonic.startswith("pop"):
            for op in insn.operands:
                if op.type == ARM_OP_REG and op.reg == ARM_REG_PC:
                    return True
        if insn.mnemonic in ("bx", "mov") and "pc" in insn.op_str:
            return True
        return insn.mnemonic in ("svc", "bkpt", "udf")

    def record_insn(self, func, insn):
        mnem = insn.mnemonic
        if mnem.startswith("ldr") and len(insn.operands) >= 2:
            mem = insn.operands[1]
            if mem.type == ARM_OP_MEM and mem.mem.base == ARM_REG_PC and mem.mem.disp is not None:
                target = (insn.address + 4 + mem.mem.disp) & ~3
                value = self.word(target)
                if value is not None:
                    func.literals[target] = value
                    self.literal_totals[value].add(func.entry)
                    self.note_constant(func, value, target)

    def record_pc_relative(self, func, insn):
        imm = None
        for op in insn.operands:
            if op.type == ARM_OP_IMM:
                imm = op.imm
                break
        if imm is None:
            return
        candidates = []
        if insn.id == ARM_INS_ADR:
            candidates.append(((insn.address + 4) & ~3) + imm)
        else:
            candidates.append(insn.address + 4 + imm)
            candidates.append(((insn.address + 4) & ~3) + imm)
            candidates.append(insn.address + 2 + imm)
        for cand in candidates:
            hit = self.string_at(cand)
            if hit is not None:
                start, text = hit
                func.strings.setdefault(start, text)
                return
            value = self.word(cand)
            if value is not None and FLASH_LO <= value < FLASH_HI:
                hit = self.string_at(value)
                if hit is not None:
                    start, text = hit
                    func.strings.setdefault(start, text)
                    return

    def note_constant(self, func, value, pool_addr):
        kind = self.classify_constant(value)
        if kind == "ram":
            func.ram_refs.add(value)
            return
        if kind == "flash":
            hit = self.string_at(value)
            if hit is not None:
                start, text = hit
                func.strings.setdefault(start, text)
            return
        name = self.attribute_peripheral(value)
        if name is not None:
            func.peripherals.setdefault(name, set()).add(value)
            self.peripheral_totals[name].add(value)


    def function_containing(self, addr):
        best = None
        for func in self.functions.values():
            if not func.insns:
                continue
            lo = func.insns[0].address
            hi = max(i.address for i in func.insns)
            if lo <= addr <= hi:
                if best is None or hi - lo < best[2] - best[1]:
                    best = (func, lo, hi)
        return best[0] if best else None

    def writers_of(self, target):
        found = []
        for func in sorted(self.functions.values(), key=lambda f: f.entry):
            regs = {}
            for insn in func.insns:
                mnem = insn.mnemonic
                if mnem.startswith("ldr") and len(insn.operands) >= 2:
                    dst = insn.operands[0]
                    mem = insn.operands[1]
                    if (mem.type == ARM_OP_MEM and mem.mem.base == ARM_REG_PC
                            and dst.type == ARM_OP_REG):
                        pool = (insn.address + 4 + mem.mem.disp) & ~3
                        value = self.word(pool)
                        if value is not None and RAM_LO <= value < RAM_HI:
                            regs[dst.reg] = value
                elif mnem in ("add", "addw", "sub", "subw", "mov", "movw", "movs") and insn.operands:
                    dst = insn.operands[0]
                    src = insn.operands[1] if len(insn.operands) > 1 else None
                    imm = None
                    for op in insn.operands[1:]:
                        if op.type == ARM_OP_IMM:
                            imm = op.imm
                    if (dst.type == ARM_OP_REG and src is not None
                            and src.type == ARM_OP_REG and src.reg in regs):
                        value = regs[src.reg]
                        if imm is not None:
                            value += imm if mnem.startswith("add") else -imm
                        regs[dst.reg] = value
                    elif dst.type == ARM_OP_REG and imm is not None and mnem.startswith("mov"):
                        regs[dst.reg] = imm
                elif mnem.startswith("str") and len(insn.operands) >= 2:
                    src = insn.operands[0]
                    mem = insn.operands[1]
                    if (mem.type == ARM_OP_MEM and mem.mem.base in regs
                            and mem.mem.disp is not None):
                        base = regs[mem.mem.base]
                        addr = base + mem.mem.disp
                        if addr == target:
                            found.append((func.entry, insn.address, mnem,
                                          src.type == ARM_OP_REG and insn.op_str))
        return found

    def refs_to(self, target):
        seen = set()
        for func in sorted(self.functions.values(), key=lambda f: f.entry):
            for pool, value in func.literals.items():
                if value == target and (func.entry, pool) not in seen:
                    seen.add((func.entry, pool))
                    print("%s literal-pool %s = %s" % (hex(func.entry), hex(pool), hex(value)))
        for off in range(0, len(self.blob) - 3, 4):
            value = struct.unpack_from("<I", self.blob, off)[0]
            if value == target:
                pool = self.base + off
                owner = self.function_containing(pool)
                if owner is not None and (owner.entry, pool) not in seen:
                    seen.add((owner.entry, pool))
                    print("%s literal-pool %s = %s" % (hex(owner.entry), hex(pool), hex(value)))


def load_image(args):
    base, blob = load_ihex(args.hex)
    if args.base is not None:
        base = args.base
    return blob, base


def cmd_strings(args, an):
    rows = [(a, t) for a, t in sorted(an.strings.items())]
    if args.grep:
        needle = args.grep.lower()
        rows = [(a, t) for a, t in rows if needle in t.lower()]
    if args.fat:
        rows = [(a, t) for a, t in rows if "\\" in t]
    for a, t in rows:
        print("%s %r" % (hex(a), t))
    print("%d strings" % len(rows), file=sys.stderr)


def cmd_xrefs(args, an):
    target = int(args.address, 0)
    hit = an.string_at(target)
    for func in sorted(an.functions.values(), key=lambda f: f.entry):
        if target in func.strings or target in func.literals.values():
            print(hex(func.entry), "strings=", [hex(k) for k in func.strings],
                  "literals=", [hex(v) for v in func.literals.values() if v == target])
    if hit is None:
        refs = [f.entry for f in an.functions.values() if target in f.literals.values()]
        print("literal refs:", [hex(x) for x in sorted(refs)])


def cmd_func(args, an):
    target = int(args.address, 0)
    best = None
    for func in an.functions.values():
        if func.entry == target:
            best = func
            break
    if best is None:
        for func in an.functions.values():
            if any(i.address == target for i in func.insns):
                best = func
                break
    if best is None:
        print("no function at", hex(target))
        return
    print(json.dumps(best.to_dict(), indent=1))
    print("--- disassembly")
    for insn in best.insns:
        print("%08x  %-8s %s" % (insn.address, insn.mnemonic, insn.op_str))


def cmd_callers(args, an):
    target = int(args.address, 0)
    func = an.functions.get(target)
    if func is None:
        print("no function at", hex(target))
        return
    print("callers:", [hex(c) for c in sorted(func.callers)])
    print("callees:", [hex(c) for c in sorted(func.callees)])
    for c in sorted(func.callees):
        sub = an.functions.get(c)
        if sub is not None:
            print("  %s strings=%s" % (hex(c), [hex(k) for k in sorted(sub.strings)]))


def cmd_range(args, an):
    lo = int(args.lo, 0)
    hi = int(args.hi, 0)
    for func in sorted(an.functions.values(), key=lambda f: f.entry):
        if not (lo <= func.entry < hi):
            continue
        print("%s insns=%-4d strings=%s callees=%s"
              % (hex(func.entry), len(func.insns),
                 [(hex(k), v[:40]) for k, v in sorted(func.strings.items())],
                 [hex(c) for c in sorted(func.callees)]))


def cmd_refs(args, an):
    target = int(args.address, 0)
    hit = an.string_at(target)
    if hit is not None:
        print("string %r @ %s" % (hit[1], hex(hit[0])))
    an.refs_to(target)


def cmd_writers(args, an):
    target = int(args.address, 0)
    for entry, at, mnem, ops in an.writers_of(target):
        print("%s %s %s %s" % (hex(entry), hex(at), mnem, ops))


def cmd_census(args, an):
    print("functions:", len(an.functions))
    print("strings:", len(an.strings))
    grouped = defaultdict(set)
    for name, values in an.peripheral_totals.items():
        grouped[name.split("+")[0]] |= values
    refs = defaultdict(int)
    for func in an.functions.values():
        for name in func.peripherals:
            refs[name.split("+")[0]] += 1
    print("--- peripheral constants")
    for name in sorted(grouped):
        print("%-16s refs=%-4d distinct=%-4d values=%s"
              % (name, refs[name], len(grouped[name]),
                 " ".join(hex(v) for v in sorted(grouped[name]))))
    print("--- RAM references")
    ram = set()
    for func in an.functions.values():
        ram |= func.ram_refs
    print("distinct RAM addresses:", len(ram))
    print("range: %s..%s" % (hex(min(ram)) if ram else "-",
                              hex(max(ram)) if ram else "-"))


STRING_TABLE = 0x080897C0


def parse_string_index(an, table=STRING_TABLE):
    """Decode the firmware's packed string index.

    Each entry is a 32-bit word at an 8-byte stride:

        bits 0..15   length of the string
        bits 16..31  character offset inside the group block
        bits 16..31? no: bits 16..31 hold the group id in the high half

    The word layout recovered from the image is

        word = (group << 16) | (offset << 4) | length

    and the zero word that follows every entry is padding.
    """
    best = []
    for start in range(table, table + 0x24, 4):
        entries = []
        addr = start
        prev_group = 0
        while addr < table + 0x2000:
            word = an.word(addr)
            if word is None:
                break
            group = (word >> 16) & 0xFFFF
            packed = word & 0xFFFF
            offset = packed >> 4
            length = packed & 0xF
            if group < prev_group or group > 0x400 or length == 0 or offset > 0x4000:
                break
            entries.append((addr, group, offset, length))
            prev_group = group
            addr += 4
        if len(entries) > len(best):
            best = entries
    return best


def group_bases(an, entries):
    """Find the base address of every group's string block.

    A group's strings are packed without terminators, so an entry's
    offset only makes sense against the start of that group's block. The
    base is found by looking for the address at which every entry of the
    group lands on a printable run of exactly the recorded length.
    """
    groups = {}
    for _, group, _, _ in entries:
        groups.setdefault(group, [])
    for _, group, offset, length in entries:
        groups[group].append((offset, length))

    bases = {}
    for group, items in groups.items():
        offset, length = items[0]
        scored = []
        for addr in range(an.base, an.base + len(an.blob) - length - offset):
            hits = sum(1 for off, ln in items
                       if an.raw_string(addr + off, ln) is not None)
            if hits == len(items):
                scored.append((hits, addr))
        scored.sort(reverse=True)
        bases[group] = [addr for _, addr in scored]
    return bases


def load_flat(path, base=None):
    """Load a flat binary or an Intel HEX file."""
    with open(path, "rb") as fh:
        head = fh.read(2)
    if head == b":\x00" or head[:1] == b":":
        from hex2elf import load_ihex
        b, blob = load_ihex(path)
        return (base if base is not None else b), blob
    with open(path, "rb") as fh:
        blob = fh.read()
    return (base if base is not None else 0), blob


ASSET_RE = re.compile(r"([A-Za-z0-9_.\\-]{1,64}\.bmp)")


def asset_names(an):
    names = set()
    for _, text in an.strings.items():
        for m in ASSET_RE.finditer(text):
            s = m.group(1).replace('/', '\\')
            if s.startswith('\\'):
                s = s[1:]
            if re.match(r"^[A-Za-z0-9_.\\-]+\.bmp$", s):
                names.add(s)
    return names


def cmd_compare(args, an):
    base2, blob2 = load_flat(args.other, args.other_base)
    other = Analyzer(blob2, base2).analyze()

    mine_assets = asset_names(an)
    their_assets = asset_names(other)
    mine_periph = set()
    for name in an.peripheral_totals:
        mine_periph.add(name.split("+")[0])
    their_periph = set()
    for name in other.peripheral_totals:
        their_periph.add(name.split("+")[0])

    print("comparison: original vs %s" % args.other)
    print("  functions:      %-6d %d" % (len(an.functions), len(other.functions)))
    print("  strings:        %-6d %d" % (len(an.strings), len(other.strings)))
    print("  asset names:    %-6d %d" % (len(mine_assets), len(their_assets)))
    print("  peripherals:    %-6d %d" % (len(mine_periph), len(their_periph)))
    print()
    print("  peripherals only in the original: %s"
          % (", ".join(sorted(mine_periph - their_periph)) or "-"))
    print("  peripherals only in %s: %s" % (args.label,
          ", ".join(sorted(their_periph - mine_periph)) or "-"))
    print("  assets only in the original: %d" % len(mine_assets - their_assets))
    print("  assets only in %s: %d" % (args.label, len(their_assets - mine_assets)))
    missing = sorted(mine_assets - their_assets)
    if missing and args.verbose:
        for m in missing:
            print("      missing: %s" % m)


def cmd_lit(args, an):
    """Find the functions whose literal pools hold a given value.

    The peripheral census counts *functions* that mention a peripheral, but
    not *where*. This answers "which functions touch this address".
    """
    needle = int(args.value, 0)
    mode = args.mode
    for func in sorted(an.functions.values(), key=lambda f: f.entry):
        hits = []
        for pool, value in func.literals.items():
            if mode == "value" and value == needle:
                hits.append((pool, value))
            elif mode == "prefix" and (value & 0xFFFFF000) == (needle & 0xFFFFF000):
                hits.append((pool, value))
            elif mode == "ram" and RAM_LO <= value < RAM_HI \
                    and 0 <= value - needle < 0x400:
                hits.append((pool, value))
        if not hits:
            continue
        strings = sorted((hex(k), v[:48]) for k, v in func.strings.items())
        print("%08x insns=%-4d %s" % (func.entry, len(func.insns), strings))
        for pool, value in sorted(hits):
            print("        pool %08x = %08x" % (pool, value))


def cmd_table(args, an):
    """Dump a data region as 32-bit words, decoding any embedded strings."""
    start = int(args.address, 0)
    count = int(args.count, 0)
    for i in range(count):
        addr = start + 4 * i
        value = an.word(addr)
        if value is None:
            break
        text = ""
        if FLASH_LO <= value < FLASH_HI:
            hit = an.string_at(value)
            if hit is not None:
                text = repr(hit[1])
        print("%08x %08x %s" % (addr, value, text))


def cmd_string_table(args, an):
    table = int(args.address, 0)
    entries = parse_string_index(an, table)
    bases = group_bases(an, entries)
    limit = int(args.count, 0)
    shown = 0
    for addr, group, offset, length in entries:
        if limit and shown >= limit:
            break
        candidates = bases.get(group) or []
        text = ""
        if candidates:
            text = an.raw_string(candidates[0] + offset, length) or ""
        print("%s group=%-4d offset=0x%04x len=%-3d base=%s %r"
              % (hex(addr), group, offset, length,
                 hex(candidates[0]) if candidates else "?", text))
        shown += 1
    print("%d entries, %d groups" % (len(entries), len(bases)), file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--hex", required=True)
    ap.add_argument("--base", type=lambda x: int(x, 0), default=None)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("strings")
    p.add_argument("--grep")
    p.add_argument("--fat", action="store_true")
    p.set_defaults(fn=cmd_strings)

    p = sub.add_parser("xrefs")
    p.add_argument("address")
    p.set_defaults(fn=cmd_xrefs)

    p = sub.add_parser("func")
    p.add_argument("address")
    p.set_defaults(fn=cmd_func)

    p = sub.add_parser("callers")
    p.add_argument("address")
    p.set_defaults(fn=cmd_callers)

    p = sub.add_parser("census")
    p.set_defaults(fn=cmd_census)

    p = sub.add_parser("range")
    p.add_argument("lo")
    p.add_argument("hi")
    p.set_defaults(fn=cmd_range)

    p = sub.add_parser("refs")
    p.add_argument("address")
    p.set_defaults(fn="refs")

    p = sub.add_parser("writers")
    p.add_argument("address")
    p.set_defaults(fn=cmd_writers)

    p = sub.add_parser("compare")
    p.add_argument("other")
    p.add_argument("--other-base", type=lambda x: int(x, 0), default=None)
    p.add_argument("--label", default="rebuild")
    p.add_argument("--verbose", action="store_true")
    p.set_defaults(fn=cmd_compare)

    p = sub.add_parser("string-table")
    p.add_argument("address")
    p.add_argument("count", nargs="?", default="64")
    p.set_defaults(fn=cmd_string_table)

    p = sub.add_parser("lit")
    p.add_argument("value")
    p.add_argument("--mode", choices=("value", "prefix", "ram"),
                   default="value")
    p.set_defaults(fn=cmd_lit)

    p = sub.add_parser("table")
    p.add_argument("address")
    p.add_argument("count", nargs="?", default="64")
    p.set_defaults(fn=cmd_table)

    args = ap.parse_args()
    blob, base = load_image(args)
    an = Analyzer(blob, base).analyze()
    if args.cmd == "refs":
        cmd_refs(args, an)
    else:
        args.fn(args, an)


if __name__ == "__main__":
    main()
