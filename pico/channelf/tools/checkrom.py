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
ROM_SIZE = 0x4000          # FN_ROM_SIZE: a FujiNet client is exactly this
CLAIM_OFF = ROM_SIZE - 4   # FN_ROM_CLAIM
CLAIM = b"FUJI"


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    if not args:
        sys.exit("usage: checkrom.py <image.bin> [expected-size] [--claim]")
    path = args[0]
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

    if len(args) > 1:
        want = int(args[1], 0)
        if len(img) != want:
            errs.append("image is %d bytes, expected %d" % (len(img), want))

    # A FujiNet client declares itself by carrying "FUJI" at the top of the 16K
    # window. Only an exactly-16K image reaches that offset, which is what keeps
    # a commercial Videocart from claiming the mailbox by accident -- and what
    # makes a client that is short, or that forgot the ORG, fail here instead of
    # silently booting with the mailbox dead.
    if "--claim" in flags:
        if len(img) != ROM_SIZE:
            errs.append("a client image must be exactly %d bytes, got %d"
                        % (ROM_SIZE, len(img)))
        elif img[CLAIM_OFF:CLAIM_OFF + 4] != CLAIM:
            errs.append("no %r claim at $%04X (found %r); the mailbox would go "
                        "dead at boot" % (CLAIM.decode(), ROM_BASE + CLAIM_OFF,
                                          bytes(img[CLAIM_OFF:CLAIM_OFF + 4])))

    if errs:
        for e in errs:
            print("checkrom: %s: %s" % (path, e), file=sys.stderr)
        return 1
    print("checkrom: %s: %d bytes, $%04X-$%04X, signature ok%s"
          % (path, len(img), ROM_BASE, ROM_BASE + len(img) - 1,
             ", claim ok" if "--claim" in flags else ""))
    return 0


sys.exit(main(sys.argv))
