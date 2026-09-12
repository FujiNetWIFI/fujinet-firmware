#!/usr/bin/env python3
"""mkcorpus.py -- synthesise a cartridge corpus for the soak.

There is no Atari 2600 ROM set on this machine -- MAME ships none and the
`a2600` software list is names, not bytes. The Channel F port hit the same wall
and did the same thing: generate a corpus that covers what the soak is actually
testing, which is the PUSH and SERVE path, not anyone's game logic.

Each image is built so a wrong byte cannot hide:

  - every byte is a function of its absolute offset, so a bank served from the
    wrong place, an off-by-one in the staging buffer, or a truncated push all
    show up as a mismatch rather than as plausible-looking data;
  - the first bytes of each bank carry the bank number, so a bank mix-up is
    visible at a glance in a failure message;
  - the reset vector points into the window, so the image is a legal cartridge
    and MAME will run it rather than refusing;
  - NO "FUJI" claim, so every one of them is a GAME: the cartridge must route
    it to its real board and the mailbox must go dead.

Each image gets a `.cfg` sibling naming its board. This is not decoration: an
8K F8, E0, UA and FE are all 8192 bytes and nothing inside the file tells them
apart, so without it the cartridge would serve three of the four as F8 and the
soak would be checking a scheme the hardware never selected. MediaTypeROM
pushes the sibling ahead of the ROM on every platform already -- the
ColecoVision port added it for exactly this reason.

Usage: mkcorpus.py <outdir>
"""

import os
import sys

# (name, scheme, size). The schemes are the ones vcsmap implements, which are
# the ones MAME implements, which are the ones that can be verified at all.
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

    # A Super Chip cartridge leaves its first 256 bytes -- the RAM window --
    # as a repeated pattern rather than code, which is how MAME's
    # detect_super_chip() recognises one. Match it, so the detector fires.
    if scheme.endswith("SC"):
        for i in range(256):
            img[i] = 0x00

    # A legal reset vector, in the bank the console powers up in. For the
    # banked schemes that is the last 4K the hardware maps; for FLAT and CV it
    # is the only one there is.
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
            f.write("%s\n" % scheme)
        assert b"FUJI" not in data, "%s accidentally claims the mailbox" % name
        print("%-12s %-5s %6d bytes -> %s" % (name, scheme, size, path))


if __name__ == "__main__":
    main()
