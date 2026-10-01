#!/usr/bin/env python3
"""checkrom.py -- validate a built NES FujiNet client image.

Enforces the layout contract in firmware/include/fuji_mailbox.h so a client
that breaks it fails its build rather than a debugging session:

  - a 16-byte iNES header the cart's own parser (nesmap.c's rules) accepts,
    and a file exactly as long as the header promises;
  - with --claim, "FUJI" in the last 16 bytes of PRG ($FFF0), which is what
    keeps the mailbox alive after the cart boots the image;
  - a reset vector inside $8000-$FFFF;
  - no read-modify-write instruction whose operand is in the mailbox's
    write-only pages ($5500-$57FF). An RMW writes the old value back on the
    cycle before the new one, and on a page where any write is an event that
    is a spurious event. Indexed stores are fine here: their extra cycle is a
    read, and reads of these pages are inert.

The scan walks the CODE-class segments named in the ld65 map (-m), so string
tables and dispatch tables are never mistaken for instructions.

Usage: checkrom.py [--claim] [--map file.map] image.nes
       checkrom.py --loader loader.bin
"""

import re
import sys

WR_LO, WR_HI = 0x5500, 0x57FF

# Read-modify-write opcodes with absolute / absolute,X operands, official and
# the illegal ones an assembler could emit for them.
RMW = {
    0x0E: "ASL abs", 0x1E: "ASL abs,X", 0x2E: "ROL abs", 0x3E: "ROL abs,X",
    0x4E: "LSR abs", 0x5E: "LSR abs,X", 0x6E: "ROR abs", 0x7E: "ROR abs,X",
    0xCE: "DEC abs", 0xDE: "DEC abs,X", 0xEE: "INC abs", 0xFE: "INC abs,X",
    0x0F: "SLO abs", 0x1F: "SLO abs,X", 0x2F: "RLA abs", 0x3F: "RLA abs,X",
    0x4F: "SRE abs", 0x5F: "SRE abs,X", 0x6F: "RRA abs", 0x7F: "RRA abs,X",
    0xCF: "DCP abs", 0xDF: "DCP abs,X", 0xEF: "ISC abs", 0xFF: "ISC abs,X",
}
JMP_IND = 0x6C

LEN = [1] * 256
for op in (0x69, 0x29, 0xC9, 0xE0, 0xC0, 0x49, 0xA9, 0xA2, 0xA0, 0x09, 0xE9,
           0xA5, 0xA6, 0xA4, 0x85, 0x86, 0x84, 0x65, 0x25, 0x06, 0x24, 0xC5,
           0xC6, 0x45, 0xE6, 0x46, 0x26, 0x66, 0xE5, 0x05, 0x75, 0x35, 0x16,
           0xD5, 0xD6, 0x55, 0xF6, 0x56, 0x36, 0x76, 0xF5, 0x15, 0xB5, 0xB4,
           0x95, 0x94, 0xB6, 0x96, 0x61, 0x21, 0xC1, 0x41, 0xA1, 0x01, 0xE1,
           0x81, 0x71, 0x31, 0xD1, 0x51, 0xB1, 0x11, 0xF1, 0x91,
           0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0):
    LEN[op] = 2
for op in (0x6D, 0x2D, 0x0E, 0x2C, 0xCD, 0xEC, 0xCC, 0xCE, 0x4D, 0xEE, 0x4C,
           0x20, 0xAD, 0xAE, 0xAC, 0x4E, 0x0D, 0x2E, 0x6E, 0xED, 0x8D, 0x8E,
           0x8C, 0x7D, 0x3D, 0x1E, 0xDD, 0xDE, 0x5D, 0xFD, 0xFE, 0x5E, 0xBD,
           0xBC, 0x3E, 0x7E, 0x1D, 0x9D, 0x79, 0x39, 0xD9, 0x59, 0xB9, 0xBE,
           0x19, 0xF9, 0x99, 0x6C):
    LEN[op] = 3

CODE_SEGS = ("CODE", "STARTUP", "LOWCODE", "ONCE", "LOADER")


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


def scan(code, base, label):
    bad = []
    pc = 0
    while pc < len(code) - 2:
        op = code[pc]
        n = LEN[op]
        if n == 3:
            tgt = code[pc + 1] | (code[pc + 2] << 8)
            if op in RMW and WR_LO <= tgt <= WR_HI:
                bad.append("%s $%04X: %s targets the write-only page at $%04X -- "
                           "the dummy write lands as a spurious event"
                           % (label, base + pc, RMW[op], tgt))
            if op == JMP_IND and (tgt & 0xFF) == 0xFF:
                bad.append("%s $%04X: JMP ($%04X) hits the 6502 page-wrap bug"
                           % (label, base + pc, tgt))
        pc += n
    return bad


def check_image(path, claim, mapfile):
    img = open(path, "rb").read()
    bad = []
    if len(img) < 16 or img[:4] != b"NES\x1a":
        return ["no iNES header"]
    prg = img[4] * 16384
    chr_ = img[5] * 8192
    if img[6] & 0x04:
        bad.append("trainer flag set; the cart refuses trainers")
    nes2 = (img[7] & 0x0C) == 0x08
    if nes2:
        prg = ((img[9] & 0x0F) << 8 | img[4]) * 16384
        chr_ = ((img[9] >> 4) << 8 | img[5]) * 8192
    if len(img) != 16 + prg + chr_:
        bad.append("file is %d bytes, header promises %d" % (len(img), 16 + prg + chr_))
    if prg == 0:
        return bad + ["no PRG"]
    prgdata = img[16:16 + prg]
    vec = prgdata[-4] | (prgdata[-3] << 8)
    if vec < 0x8000:
        bad.append("reset vector $%04X is not in $8000-$FFFF" % vec)
    if claim and prgdata[-16:-12] != b"FUJI":
        bad.append("no \"FUJI\" claim at PRG end-16 ($FFF0): the mailbox would go dead at boot")

    ranges = code_ranges(mapfile)
    if not ranges:
        # no map: scan the whole PRG as code (noisy but safe)
        ranges = [(0x8000, 0x8000 + min(prg, 0x8000) - 1)]
    for lo, hi in ranges:
        # PRG is mapped $8000+ for a 32K image; bigger images are scanned by
        # their linear offset from $8000 for the first 32K only.
        off = lo - 0x8000
        if off < 0 or off >= len(prgdata):
            continue
        end = min(hi + 1 - 0x8000, len(prgdata))
        bad += scan(prgdata[off:end], lo, "code $%04X-$%04X" % (lo, hi))
    return bad


def check_loader(path):
    img = open(path, "rb").read()
    bad = []
    if len(img) != 0x800:
        bad.append("loader is %d bytes, must be exactly 2048" % len(img))
    if img[0] != 0x4C:
        bad.append("no JMP at $5800")
    if img[3] != 0x40:
        bad.append("no RTI at $5803 (the cart's NMI/IRQ vectors point there)")
    bad += scan(img, 0x5800, "loader")
    return bad


def main():
    args = sys.argv[1:]
    claim = False
    loader = False
    mapfile = None
    paths = []
    while args:
        a = args.pop(0)
        if a == "--claim":
            claim = True
        elif a == "--loader":
            loader = True
        elif a == "--map":
            mapfile = args.pop(0)
        else:
            paths.append(a)
    rc = 0
    for p in paths:
        problems = check_loader(p) if loader else check_image(p, claim, mapfile)
        for x in problems:
            print("checkrom: %s: %s" % (p, x), file=sys.stderr)
        if problems:
            rc = 1
        else:
            print("checkrom: %s: ok" % p)
    return rc


if __name__ == "__main__":
    sys.exit(main())
