#!/usr/bin/env python3
"""checkrom.py -- validate a built 7800 FujiNet client image, or the loader.

Enforces the layout contract in firmware/include/fuji_mailbox.h so a client
that breaks it fails its build rather than a debugging session:

  - a headerless image whose size is a whole number of KiB, or an .a78 file
    (128-byte header) as MAME loads one;
  - a reset vector at $4000 or above;
  - with --claim, "FUJI" at $FF70, which keeps the mailbox alive after boot;
  - no read-modify-write instruction aimed at the mailbox's write pages
    ($0D00-$0FFF): an RMW writes the old value back first, and there every
    write is an event;
  - with --notia, no absolute or zero-page store to the TIA's range ($00-$1F
    and its mirrors): until something locks it, every such write replaces
    INPTCTRL, and CONFIG must leave it unlocked so a game can be started by
    the console's own BIOS. Indexed stores cannot be checked here; the cart's
    INPTCTRL model reports them at run time;
  - no JMP ($xxFF): the 6502 fetches its high byte from the wrong page.

The scan walks the code segments named in the ld65 map (-m), so tables are
never mistaken for instructions.

Usage: checkrom.py [--claim] [--notia] [--map file.map] image.{bin,a78}
       checkrom.py --loader --map loader.map loader.bin
"""

import re
import sys

WR_LO, WR_HI = 0x0D00, 0x0FFF
CLAIM = 0xFF70

RMW = {
    0x0E: "ASL abs", 0x1E: "ASL abs,X", 0x2E: "ROL abs", 0x3E: "ROL abs,X",
    0x4E: "LSR abs", 0x5E: "LSR abs,X", 0x6E: "ROR abs", 0x7E: "ROR abs,X",
    0xCE: "DEC abs", 0xDE: "DEC abs,X", 0xEE: "INC abs", 0xFE: "INC abs,X",
    0x0F: "SLO abs", 0x1F: "SLO abs,X", 0x2F: "RLA abs", 0x3F: "RLA abs,X",
    0x4F: "SRE abs", 0x5F: "SRE abs,X", 0x6F: "RRA abs", 0x7F: "RRA abs,X",
    0xCF: "DCP abs", 0xDF: "DCP abs,X", 0xEF: "ISC abs", 0xFF: "ISC abs,X",
}
# stores and RMWs that write a fixed address: zero page and absolute
ZP_WRITES = {0x85, 0x86, 0x84, 0x06, 0x26, 0x46, 0x66, 0xC6, 0xE6}
ABS_WRITES = {0x8D, 0x8E, 0x8C, 0x0E, 0x2E, 0x4E, 0x6E, 0xCE, 0xEE}
JMP_IND = 0x6C

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

CODE_SEGS = ("CODE", "STARTUP", "LOWCODE", "ONCE", "LOADER", "ENTRY")


def in_tia(a):
    return (a & 0xFCE0) == 0


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
        if len(parts) >= 4 and parts[0] in CODE_SEGS:
            ranges.append((int(parts[1], 16), int(parts[2], 16)))
    return ranges


def scan(code, stop, base, label, notia):
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
            if op in RMW and WR_LO <= tgt <= WR_HI:
                bad.append("%s $%04X: %s targets the write page at $%04X -- "
                           "the dummy write lands as a spurious event"
                           % (label, base + pc, RMW[op], tgt))
            if notia and op in ABS_WRITES and in_tia(tgt):
                bad.append("%s $%04X: a store to $%04X rewrites INPTCTRL"
                           % (label, base + pc, tgt))
            if op == JMP_IND and (tgt & 0xFF) == 0xFF:
                bad.append("%s $%04X: JMP ($%04X) hits the 6502 page-wrap bug"
                           % (label, base + pc, tgt))
        elif n == 2 and notia and op in ZP_WRITES and code[pc + 1] < 0x20:
            bad.append("%s $%04X: a store to $%02X rewrites INPTCTRL"
                       % (label, base + pc, code[pc + 1]))
        pc += n
    return bad


def check_image(path, claim, notia, mapfile):
    img = open(path, "rb").read()
    bad = []
    if len(img) >= 128 and img[1:10] == b"ATARI7800":
        img = img[128:]
    if not img or len(img) % 1024:
        return ["image is %d bytes, not a whole number of KiB" % len(img)]
    top = img[-0x10000:] if len(img) > 0x10000 else img
    base = 0x10000 - len(top)                     # top-aligned, as the cart maps it
    mem = lambda a: top[a - base] if a >= base else 0xFF
    vec = mem(0xFFFC) | (mem(0xFFFD) << 8)
    if vec < 0x4000:
        bad.append("reset vector $%04X is below $4000" % vec)
    if claim and bytes(mem(CLAIM + i) for i in range(4)) != b"FUJI":
        bad.append("no \"FUJI\" claim at $FF70: the mailbox would go dead at boot")

    ranges = code_ranges(mapfile) or [(max(base, 0x4000), 0xFFFF)]
    for lo, hi in ranges:
        if lo < base or hi > 0xFFFF:
            continue
        bad += scan(top[lo - base:], hi + 1 - lo, lo, "code $%04X-$%04X" % (lo, hi), notia)
    return bad


def check_loader(path, mapfile):
    img = open(path, "rb").read()
    bad = []
    if len(img) != 0x200:
        bad.append("loader is %d bytes, must be exactly 512" % len(img))
    if img[0] != 0x4C or img[3] != 0x4C:
        bad.append("no JMP at $0600 and $0603")
    for lo, hi in code_ranges(mapfile) or [(0x0600, 0x07FF)]:
        bad += scan(img[lo - 0x0600:], hi + 1 - lo, lo, "loader", True)
    return bad


def main():
    args = sys.argv[1:]
    claim = notia = loader = False
    mapfile = None
    paths = []
    while args:
        a = args.pop(0)
        if a == "--claim":
            claim = True
        elif a == "--notia":
            notia = True
        elif a == "--loader":
            loader = True
        elif a == "--map":
            mapfile = args.pop(0)
        else:
            paths.append(a)
    rc = 0
    for p in paths:
        problems = check_loader(p, mapfile) if loader else check_image(p, claim, notia, mapfile)
        for x in problems:
            print("checkrom: %s: %s" % (p, x), file=sys.stderr)
        if problems:
            rc = 1
        else:
            print("checkrom: %s: ok" % p)
    return rc


if __name__ == "__main__":
    sys.exit(main())
