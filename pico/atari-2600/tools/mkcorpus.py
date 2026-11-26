#!/usr/bin/env python3
"""mkcorpus.py -- synthesize a cartridge corpus for the soak.

Every byte is a function of its offset, so a misplaced or truncated bank shows
as a mismatch. No image claims "FUJI": each is a GAME the cart must serve on
its own board. Each gets a .cfg sibling (`mapper=F8`), since an 8K F8, E0, UA
and FE are the same size and nothing in the file tells them apart.

Usage: mkcorpus.py <outdir>
"""

import os
import sys

# (name, scheme, size): every scheme vcsmap.h implements.
CORPUS = [
    ("soak2k",   "FLAT", 2048),
    ("soak4k",   "FLAT", 4096),
    ("soakf8",   "F8",   8192),
    ("soakf8sc", "F8SC", 8192),
    ("soakfa",   "FA",  12288),
    ("soakf6",   "F6",  16384),
    ("soakf6sc", "F6SC", 16384),
    ("soakf4",   "F4",  32768),
    ("soakf4sc", "F4SC", 32768),
    ("soake0",   "E0",   8192),
    ("soakua",   "UA",   8192),
    ("soakfe",   "FE",   8192),
    ("soakcv",   "CV",   2048),
]

BANK = 4096


def build(scheme, size):
    img = bytearray(size)
    for i in range(size):
        # Distinct per offset, and cheap to reproduce in a checker.
        img[i] = (i * 7 + (i >> 8) * 31 + 1) & 0xFF

    nbanks = max(1, size // BANK)
    for b in range(nbanks):
        base = b * BANK
        if base + 4 <= size:
            img[base] = 0xB0 | (b & 0x0F)      # this bank, at a glance
            img[base + 1] = 0xA5

    # MAME's detect_super_chip() looks for a repeated first 256 bytes.
    if scheme.endswith("SC"):
        for i in range(256):
            img[i] = 0x00

    # A legal reset vector in the bank the console powers up in.
    if size >= BANK:
        img[size - 4] = 0x00
        img[size - 3] = 0x10                    # $1000
        img[size - 2] = 0x00
        img[size - 1] = 0x10
    else:
        img[size - 4] = 0x00
        img[size - 3] = 0x10
        img[size - 2] = 0x00
        img[size - 1] = 0x10
    return bytes(img)


def main():
    outdir = sys.argv[1]
    os.makedirs(outdir, exist_ok=True)
    for name, scheme, size in CORPUS:
        data = build(scheme, size)
        path = os.path.join(outdir, name + ".bin")
        with open(path, "wb") as f:
            f.write(data)
        with open(os.path.join(outdir, name + ".cfg"), "w") as f:
            f.write("mapper=%s\n" % scheme)
        assert b"FUJI" not in data, "%s accidentally claims the mailbox" % name
        print("%-12s %-5s %6d bytes -> %s" % (name, scheme, size, path))


if __name__ == "__main__":
    main()
