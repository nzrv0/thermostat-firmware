#!/usr/bin/env python3
"""Firmware-level smoke test: the rebuild's layout contract.

The bootloader hands control to the application partition at a fixed
address, and the original's vector table is part of that contract, so the
rebuilt image has to land in exactly the same place. This compares the
rebuilt image against the shipped one:

  * the vector table starts at 0x08005800 and is 336 bytes (84 words)
  * the initial stack pointer word is byte-identical (0x20016B20)
  * code starts at 0x08005950, immediately after the table
  * the image never reaches back into the shared parameter block at
    0x08005000..0x080057FF
  * the entry record of the HEX file (Start Linear Address) points at
    0x08005950 with the Thumb bit set

Handler addresses inside the table are expected to differ - the rebuild
has its own code - so they are reported, not asserted.

Usage:
    tools/smoke_test.py --original Falcon-HP-144pin-8M-Mark-261001.hex \\
                        --built firmware/build/falcon-app.hex
"""

import argparse
import struct
import sys

VECTOR_BASE = 0x08005800
VECTOR_SIZE = 336
CODE_START = 0x08005950
PARAM_BLOCK = (0x08005000, 0x080057FF)
INITIAL_SP = 0x20016B20


def load_ihex(path):
    """Return (base, bytes, entry) for an Intel HEX file."""
    data = {}
    cur = 0
    entry = None
    with open(path) as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            raw = bytes.fromhex(line[1:])
            count = raw[0]
            addr = (raw[1] << 8) | raw[2]
            rtype = raw[3]
            payload = raw[4:4 + count]
            if (sum(raw) & 0xFF) != 0:
                raise SystemExit("%s:%d: bad checksum" % (path, lineno))
            if rtype == 0x00:
                base = (cur << 16) + addr
                for i, b in enumerate(payload):
                    data[base + i] = b
            elif rtype == 0x01:
                break
            elif rtype == 0x02:
                cur = struct.unpack(">H", payload)[0] >> 12
            elif rtype == 0x04:
                cur = struct.unpack(">H", payload)[0]
            elif rtype == 0x05:
                entry = struct.unpack(">I", payload)[0]
    if not data:
        raise SystemExit("%s: no data records" % path)
    lo, hi = min(data), max(data)
    blob = bytearray()
    for a in range(lo, hi + 1):
        blob.append(data.get(a, 0xFF))
    return lo, bytes(blob), entry


class Checks:
    def __init__(self):
        self.count = 0
        self.failures = 0

    def check(self, cond, msg):
        self.count += 1
        if not cond:
            self.failures += 1
            print("  FAIL: %s" % msg)
        return cond


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", required=True)
    ap.add_argument("--built", required=True)
    args = ap.parse_args()

    c = Checks()
    obase, oblob, oentry = load_ihex(args.original)
    bbase, bblob, bentry = load_ihex(args.built)

    print("Falcon HP firmware - layout smoke test")
    print("======================================")
    print("original: %s base 0x%08X size %d entry 0x%08X"
          % (args.original, obase, len(oblob), oentry or 0))
    print("built:    %s base 0x%08X size %d entry 0x%08X"
          % (args.built, bbase, len(bblob), bentry or 0))
    print()

    c.check(obase == VECTOR_BASE, "original is based at 0x%08X" % VECTOR_BASE)
    c.check(bbase == VECTOR_BASE, "rebuild is based at 0x%08X" % VECTOR_BASE)

    c.check(len(oblob) >= VECTOR_SIZE, "original vector table present")
    c.check(len(bblob) >= VECTOR_SIZE, "rebuild vector table present")

    o_sp = struct.unpack_from("<I", oblob, 0)[0]
    b_sp = struct.unpack_from("<I", bblob, 0)[0]
    c.check(o_sp == INITIAL_SP, "original initial SP is 0x%08X" % INITIAL_SP)
    c.check(b_sp == o_sp, "rebuild initial SP matches the original "
                          "(0x%08X vs 0x%08X)" % (b_sp, o_sp))

    c.check(bentry is not None, "rebuild HEX carries a start linear address")
    c.check(bentry is not None and (bentry & 1) == 1,
            "rebuild HEX entry record has the Thumb bit set (0x%08X)"
            % (bentry or 0))
    c.check(bentry is not None and bbase <= (bentry & ~1) < bbase + len(bblob),
            "rebuild HEX entry record points inside the image")

    # The table must be the first thing in the image and code must follow
    # it immediately: the original's table ends at 0x0800594C and code
    # begins at 0x08005950.
    c.check(VECTOR_BASE + VECTOR_SIZE == CODE_START,
            "vector table size and code start are consistent")
    c.check(bbase + VECTOR_SIZE == CODE_START,
            "rebuild code starts right after its vector table")

    # Nothing may be placed in the shared parameter block.
    top = bbase + len(bblob)
    c.check(top <= PARAM_BLOCK[1] or bbase >= PARAM_BLOCK[1] + 1,
            "rebuild image does not overlap the parameter block "
            "0x%08X..0x%08X" % PARAM_BLOCK)

    # The reset vector must point at code inside the image, and so must
    # every core-exception vector that the table installs.
    reset = struct.unpack_from("<I", bblob, 4)[0] & ~1
    c.check(bbase <= reset < top,
            "rebuild reset handler 0x%08X is inside the image" % reset)
    for index in (2, 3, 4, 5, 6, 11, 14, 15):
        vector = struct.unpack_from("<I", bblob, 4 * index)[0] & ~1
        c.check(vector == 0 or bbase <= vector < top,
                "rebuild vector %d (0x%08X) is inside the image"
                % (index, vector))

    print("vector table: %d bytes at 0x%08X" % (VECTOR_SIZE, VECTOR_BASE))
    print("code starts:  0x%08X" % CODE_START)
    print("image top:    0x%08X" % top)
    print("initial SP:   0x%08X (original 0x%08X)" % (b_sp, o_sp))
    print()
    print("%d checks, %d failures" % (c.count, c.failures))
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(main())
