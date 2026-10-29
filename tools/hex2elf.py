#!/usr/bin/env python3
"""Wrap a flat firmware image in an ELF container for REA/Hopper.

Hopper cannot open a raw binary, and REA's raw-binary import path requires
Ghidra (not configured here). Wrapping the image in a minimal ELF32 ARM
executable lets Hopper analyse it with the correct base address.

The wrapper is deliberately minimal: one PT_LOAD segment covering the
image, plus section headers describing it as .text so the disassembler
treats the whole thing as code.

Example:
    tools/hex2elf.py Falcon-HP-144pin-8M-Mark-261001.hex \\
        --base 0x08005800 --entry 0x08005950 -o /tmp/app.elf
"""

import argparse
import struct
import sys

EI_NIDENT = 16
ELFCLASS32 = 1
ELFDATA2LSB = 1
ET_EXEC = 2
EM_ARM = 40
PT_LOAD = 1
PF_X = 1
PF_W = 2
SHT_PROGBITS = 1
SHT_STRTAB = 3
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_NULL = 0

EHDR_SIZE = 52
PHDR_SIZE = 32
SHDR_SIZE = 40


def load_ihex(path):
    """Return (base_address, bytes) for an Intel HEX file."""
    data = {}
    cur = 0
    with open(path) as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                raw = bytes.fromhex(line[1:])
            except ValueError:
                raise SystemExit("%s:%d: not valid hex" % (path, lineno))
            count = raw[0]
            addr = (raw[1] << 8) | raw[2]
            rtype = raw[3]
            payload = raw[4:4 + count]
            if (sum(raw) & 0xFF) != 0:
                raise SystemExit("%s:%d: bad checksum" % (path, lineno))
            if rtype == 0x00:      # data
                base = (cur << 16) + addr
                for i, b in enumerate(payload):
                    data[base + i] = b
            elif rtype == 0x01:    # EOF
                break
            elif rtype == 0x02:    # extended segment address
                cur = struct.unpack(">H", payload)[0] >> 12
            elif rtype == 0x04:    # extended linear address
                cur = struct.unpack(">H", payload)[0]
            elif rtype == 0x05:    # start linear address (entry point)
                pass
    if not data:
        raise SystemExit("%s: no data records" % path)

    lo, hi = min(data), max(data)
    blob = bytearray()
    for a in range(lo, hi + 1):
        blob.append(data.get(a, 0xFF))
    return lo, bytes(blob)


def build_elf(base, blob, entry, section_name=".text"):
    """Build a minimal ELF32 little-endian ARM executable."""
    ehdr_size = EHDR_SIZE
    phdr_off = ehdr_size
    data_off = phdr_off + PHDR_SIZE
    shstr = b"\x00.text\x00.shstrtab\x00"

    # shstrtab contents: \0 .text \0 .shstrtab \0
    name_text_off = 1
    name_shstr_off = 1 + len(section_name) + 1

    sh_off = data_off + len(blob)
    # align the section header table
    sh_off = (sh_off + 3) & ~3
    shstr_off = sh_off + 3 * SHDR_SIZE
    shstr_off = (shstr_off + 3) & ~3

    total = shstr_off + len(shstr)

    out = bytearray(total)

    # ELF header
    ident = bytearray(EI_NIDENT)
    ident[0:4] = b"\x7fELF"
    ident[4] = ELFCLASS32
    ident[5] = ELFDATA2LSB
    ident[6] = 1                     # EI_VERSION
    out[0:EI_NIDENT] = ident
    struct.pack_into("<HHIIIIIHHHHHH", out, EI_NIDENT,
                     ET_EXEC, EM_ARM, 1, entry, phdr_off, 0, 0,
                     ehdr_size, PHDR_SIZE, 1, SHDR_SIZE, 3, 2)

    # Program header: one PT_LOAD covering the whole image
    struct.pack_into("<IIIIIIII", out, phdr_off,
                     PT_LOAD, data_off, base, base,
                     len(blob), len(blob), PF_X | PF_W, 0x1000)

    out[data_off:data_off + len(blob)] = blob

    # Section headers: null, .text, .shstrtab
    # Each Elf32_Shdr is: name, type, flags, addr, offset, size, link,
    # info, addralign, entsize.
    struct.pack_into("<IIIIIIIIII", out, sh_off + 0 * SHDR_SIZE,
                     0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0)
    struct.pack_into("<IIIIIIIIII", out, sh_off + 1 * SHDR_SIZE,
                     name_text_off, SHT_PROGBITS,
                     SHF_ALLOC | SHF_EXECINSTR,
                     base, data_off, len(blob), 0, 0, 4, 0)
    struct.pack_into("<IIIIIIIIII", out, sh_off + 2 * SHDR_SIZE,
                     name_shstr_off, SHT_STRTAB, 0, 0,
                     shstr_off, len(shstr), 0, 0, 1, 0)

    out[shstr_off:shstr_off + len(shstr)] = shstr

    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hexfile")
    ap.add_argument("--base", type=lambda x: int(x, 0), default=None,
                    help="override the load base address")
    ap.add_argument("--entry", type=lambda x: int(x, 0), default=None,
                    help="override the entry point")
    ap.add_argument("--output", "-o", required=True)
    args = ap.parse_args()

    base, blob = load_ihex(args.hexfile)
    if args.base is not None:
        base = args.base
    entry = args.entry if args.entry is not None else base

    with open(args.output, "wb") as fh:
        fh.write(build_elf(base, blob, entry))

    print("wrote %s: base 0x%08X, entry 0x%08X, %d bytes"
          % (args.output, base, entry, len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
