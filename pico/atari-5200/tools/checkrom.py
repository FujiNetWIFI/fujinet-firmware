#!/usr/bin/env python3
"""checkrom.py -- validate a built 5200 FujiNet client image.

Enforces the contract in firmware/include/fuji_mailbox.h, so a client that
breaks it fails its build rather than a debugging session:

  - a flat 32K image, or 64K/128K for a Super Cart app;
  - an entry vector at $BFFE inside the cart window;
  - with --claim, "FUJI" at $BFE0, $02 at $BFE7 (the 2-port BIOS's PAL
    check) and $FF at $BFFD (no BIOS title screen);
  - no store into the cart window: the edge has no R/W, so the cart would
    drive the bus against the CPU, and an indexed store's dummy read of a
    hotspot page is an event;
  - no indexed read of a hotspot page ($B500-$B7FF) from a base that is not
    page-aligned: a page crossing makes the 6502 read the wrong page first;
  - no read-modify-write anywhere in the cart window;
  - no JMP ($xxFF): the 6502 fetches its high byte from the wrong page.

The scan walks the code segments named in the ld65 map (-m), so tables are
never mistaken for instructions. (zp),Y reads cannot be checked here.

Usage: checkrom.py [--claim] [--map file.map] image.bin
"""

import re
import sys

WIN_LO, WIN_HI = 0x4000, 0xBFFF
HOT_LO, HOT_HI = 0xB500, 0xB7FF
CLAIM = 0xBFE0

LEN = [1] * 256
for op in (0x69, 0x29, 0xC9, 0xE0, 0xC0, 0x49, 0xA9, 0xA2, 0xA0, 0x09, 0xE9,
           0xA5, 0xA6, 0xA4, 0x85, 0x86, 0x84, 0x65, 0x25, 0x06, 0x24, 0xC5,
           0xC6, 0x45, 0xE6, 0x46, 0x26, 0x66, 0xE5, 0x05, 0x75, 0x35, 0x16,
           0xD5, 0xD6, 0x55, 0xF6, 0x56, 0x36, 0x76, 0xF5, 0x15, 0xB5, 0xB4,
           0x95, 0x94, 0xB6, 0x96, 0x61, 0x21, 0xC1, 0x41, 0xA1, 0x01, 0xE1,
           0x81, 0x71, 0x31, 0xD1, 0x51, 0xB1, 0x11, 0xF1, 0x91, 0xC4, 0xE4,
           0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0):
    LEN[op] = 2
for op in (0x6D, 0x2D, 0x0E, 0x2C, 0xCD, 0xEC, 0xCC, 0xCE, 0x4D, 0xEE, 0x4C,
           0x20, 0xAD, 0xAE, 0xAC, 0x4E, 0x0D, 0x2E, 0x6E, 0xED, 0x8D, 0x8E,
           0x8C, 0x7D, 0x3D, 0x1E, 0xDD, 0xDE, 0x5D, 0xFD, 0xFE, 0x5E, 0xBD,
           0xBC, 0x3E, 0x7E, 0x1D, 0x9D, 0x79, 0x39, 0xD9, 0x59, 0xB9, 0xBE,
           0x19, 0xF9, 0x99, 0x6C):
    LEN[op] = 3

# absolute and absolute-indexed instructions that write their target
ABS_STORES = {0x8D: "STA abs", 0x8E: "STX abs", 0x8C: "STY abs",
              0x9D: "STA abs,X", 0x99: "STA abs,Y"}
ABS_RMW = {0x0E: "ASL abs", 0x1E: "ASL abs,X", 0x2E: "ROL abs", 0x3E: "ROL abs,X",
           0x4E: "LSR abs", 0x5E: "LSR abs,X", 0x6E: "ROR abs", 0x7E: "ROR abs,X",
           0xCE: "DEC abs", 0xDE: "DEC abs,X", 0xEE: "INC abs", 0xFE: "INC abs,X"}
# indexed reads, which cross a page when base + index does
IDX_READS = {0x7D, 0x3D, 0xDD, 0x5D, 0xFD, 0xBD, 0xBC, 0x1D,
             0x79, 0x39, 0xD9, 0x59, 0xB9, 0xBE, 0x19, 0xF9}
JMP_IND = 0x6C

CODE_SEGS = ("CODE", "STARTUP", "LOWCODE", "ONCE", "INIT", "BANKCODE")


def code_ranges(mapfile):
    """(start, end) console-address ranges of the code segments, from ld65 -m."""
    ranges = []
    if not mapfile:
        return ranges
    text = open(mapfile).read()
    m = re.search(r"Segment list:\s*\n-+\s*\n(.*?)\n\n", text, re.S)
    if not m:
        return ranges
    for line in m.group(1).splitlines():
        parts = line.split()
        if len(parts) >= 4 and (parts[0] in CODE_SEGS or parts[0].startswith("CODE")):
            ranges.append((int(parts[1], 16), int(parts[2], 16)))
    return ranges


def scan(code, stop, base, label):
    """Decode code[:stop]; the last instruction's operand may run past stop."""
    bad = []
    pc = 0
    while pc < stop:
        op = code[pc]
        n = LEN[op]
        if pc + n > len(code):
            break
        if n == 3:
            tgt = code[pc + 1] | (code[pc + 2] << 8)
            here = "%s $%04X" % (label, base + pc)
            if op in ABS_STORES and WIN_LO <= tgt <= WIN_HI:
                bad.append("%s: %s $%04X writes the cart window" % (here, ABS_STORES[op], tgt))
            if op in ABS_RMW and WIN_LO <= tgt <= WIN_HI:
                bad.append("%s: %s $%04X writes the cart window" % (here, ABS_RMW[op], tgt))
            if op in IDX_READS and HOT_LO <= tgt <= HOT_HI and tgt & 0xFF:
                bad.append("%s: indexed read of $%04X: a page crossing reads the wrong "
                           "hotspot first" % (here, tgt))
            if op == JMP_IND and (tgt & 0xFF) == 0xFF:
                bad.append("%s: JMP ($%04X) hits the 6502 page-wrap bug" % (here, tgt))
        pc += n
    return bad


def check_image(path, claim, mapfile):
    img = open(path, "rb").read()
    bad = []
    if len(img) not in (0x8000, 0x10000, 0x20000):
        return ["image is %d bytes: a client is 32K, or 64K/128K banked" % len(img)]
    last = img[-0x8000:]
    mem = lambda a: last[a - WIN_LO]
    vec = mem(0xBFFE) | (mem(0xBFFF) << 8)
    if not WIN_LO <= vec <= WIN_HI:
        bad.append("entry vector $%04X is outside the cart window" % vec)
    if claim:
        if bytes(mem(CLAIM + i) for i in range(4)) != b"FUJI":
            bad.append("no \"FUJI\" claim at $BFE0: the mailbox would go dead at boot")
        if mem(0xBFE7) != 0x02:
            bad.append("$BFE7 is $%02X, not $02: the 2-port BIOS hangs on a PAL GTIA" % mem(0xBFE7))
        if mem(0xBFFD) != 0xFF:
            bad.append("$BFFD is $%02X, not $FF: the BIOS would show its title" % mem(0xBFFD))

    banks = len(img) // 0x8000
    for lo, hi in code_ranges(mapfile) or [(WIN_LO, 0xAFFF), (0xB800, 0xBFDF)]:
        if lo < WIN_LO or hi > WIN_HI:
            continue
        for b in range(banks):
            bank = img[b * 0x8000:(b + 1) * 0x8000]
            bad += scan(bank[lo - WIN_LO:], hi + 1 - lo, lo,
                        "bank %d code $%04X-$%04X" % (b, lo, hi) if banks > 1
                        else "code $%04X-$%04X" % (lo, hi))
            if not code_ranges(mapfile):
                break
    return bad


def main():
    args = sys.argv[1:]
    claim = False
    mapfile = None
    paths = []
    while args:
        a = args.pop(0)
        if a == "--claim":
            claim = True
        elif a == "--map":
            mapfile = args.pop(0)
        else:
            paths.append(a)
    rc = 0
    for p in paths:
        problems = check_image(p, claim, mapfile)
        for x in problems:
            print("checkrom: %s: %s" % (p, x), file=sys.stderr)
        if problems:
            rc = 1
        else:
            print("checkrom: %s: ok" % p)
    return rc


if __name__ == "__main__":
    sys.exit(main())
