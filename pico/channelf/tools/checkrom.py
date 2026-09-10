#!/usr/bin/env python3
"""Enforce the Channel F cart layout on a built image.

The console BIOS compares the byte at $0800 against $55 and, only if it
matches, jumps to $0802 (verified by disassembling sl31253/sl31254, not from
documentation). An image that fails that check silently boots the BIOS's
built-in Hockey instead of the cart, which looks like a dead cartridge.
"""
import sys

ROM_BASE = 0x0800
SIG = 0x55


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: checkrom.py <image.bin> [expected-size]")
    path = argv[1]
    img = open(path, "rb").read()
    errs = []

    if not img:
        errs.append("image is empty")
    else:
        if img[0] != SIG:
            errs.append("byte at $%04X is $%02X, must be $%02X (BIOS cart signature)"
                        % (ROM_BASE, img[0], SIG))
        if len(img) < 3:
            errs.append("image is shorter than the $0802 entry point")

    if len(argv) > 2:
        want = int(argv[2], 0)
        if len(img) != want:
            errs.append("image is %d bytes, expected %d" % (len(img), want))

    if errs:
        for e in errs:
            print("checkrom: %s: %s" % (path, e), file=sys.stderr)
        return 1
    print("checkrom: %s: %d bytes, $%04X-$%04X, signature ok"
          % (path, len(img), ROM_BASE, ROM_BASE + len(img) - 1))
    return 0


sys.exit(main(sys.argv))
