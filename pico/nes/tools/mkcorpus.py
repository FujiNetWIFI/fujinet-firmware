#!/usr/bin/env python3
"""mkcorpus.py -- synthetic .nes images for the soak.

There is no NES ROM set on this machine, so the soak drives synthetic images:
one per Tier-1 mapper at the sizes that matter, every 1K PRG and CHR bank
stamped so a Lua harness can tell which bank a slot is showing:

    PRG bank b (1K units), offset 0:  'P', b_lo, b_hi, ~b_lo, then a hash run
    CHR bank b (1K units), offset 0:  'C', b_lo, b_hi, ~b_lo, then a hash run

Every 16K PRG bank ends with a reset vector into a two-byte `SEI / JMP *`
stub at its own start (offset 4), so whatever bank is fixed at $C000 at
power-on, the console idles instead of executing stamps. The last 16 bytes of
PRG carry no claim: these are "games", and the mailbox must go dead.

Usage: mkcorpus.py [outdir]   (default build/soak)
"""

import pathlib
import sys

CASES = [
    # name,        mapper, prg16, chr8, mirror(0=H,1=V), four_screen, prg_ram
    ("nrom_32k",       0,   2,  1, 1, 0, 0),
    ("nrom_16k_ram",   0,   1,  0, 0, 0, 0),
    ("mmc1_128k",      1,   8,  4, 0, 0, 1),
    ("mmc1_256k_ram",  1,  16,  0, 0, 0, 1),
    ("mmc1_512k",      1,  32,  0, 0, 0, 1),
    ("uxrom_128k",     2,   8,  0, 1, 0, 0),
    ("uxrom_256k",     2,  16,  0, 1, 0, 0),
    ("cnrom_32k",      3,   2,  4, 0, 0, 0),
    ("mmc3_256k",      4,  16, 16, 0, 0, 1),
    ("mmc3_512k",      4,  32, 32, 0, 0, 1),
    ("mmc3_ram",       4,   8,  0, 1, 0, 1),
    ("axrom_256k",     7,  16,  0, 0, 0, 0),
    ("cdreams_128k",  11,   8,  4, 0, 0, 0),
    ("unrom512",      30,  32,  0, 1, 1, 0),
    ("bnrom_128k",    34,   8,  0, 0, 0, 0),
    ("nina001",       34,   4,  8, 0, 0, 1),
    ("gxrom_128k",    66,   8,  4, 0, 0, 0),
    ("camerica_256k", 71,  16,  0, 1, 0, 0),
    ("namcot108",    206,   8,  8, 0, 0, 0),
]


def stamp(kind, b, seed):
    blk = bytearray(1024)
    blk[0] = ord(kind)
    blk[1] = b & 0xFF
    blk[2] = (b >> 8) & 0xFF
    blk[3] = (~b) & 0xFF
    for i in range(4, 1024):
        blk[i] = (i * 31 + b * 7 + seed + (i >> 6)) & 0xFF
    return blk


def image(mapper, prg16, chr8, mirror, four, ram, seed):
    hdr = bytearray(16)
    hdr[0:4] = b"NES\x1a"
    hdr[4] = prg16
    hdr[5] = chr8
    hdr[6] = (mirror & 1) | (0x08 if four else 0) | ((mapper & 0x0F) << 4)
    hdr[7] = mapper & 0xF0
    hdr[8] = 1 if ram else 0
    prg = bytearray()
    for b in range(prg16 * 16):
        prg += stamp('P', b, seed)
    # per 16K bank: an idle stub at +4 and vectors at the end pointing to it
    for k in range(prg16):
        base = k * 16384
        prg[base + 4:base + 8] = bytes([0x78, 0x4C, 0x04, 0x80])   # SEI; JMP $8004
        vec = base + 16384 - 6
        prg[vec:vec + 6] = bytes([0x04, 0x80, 0x04, 0x80, 0x04, 0x80])
    chr_ = bytearray()
    for b in range(chr8 * 8):
        chr_ += stamp('C', b, seed)
    return bytes(hdr + prg + chr_)


def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/soak")
    out.mkdir(parents=True, exist_ok=True)
    for i, (name, mapper, prg16, chr8, mirror, four, ram) in enumerate(CASES):
        img = image(mapper, prg16, chr8, mirror, four, ram, seed=i * 37 + 11)
        p = out / f"soak_{name}.nes"
        p.write_bytes(img)
        print("  %-24s mapper %3d  %7d bytes" % (p.name, mapper, len(img)))
    print("mkcorpus: %d images in %s" % (len(CASES), out))


if __name__ == "__main__":
    main()
