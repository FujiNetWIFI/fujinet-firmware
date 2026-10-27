#!/usr/bin/env python3
"""Enforce the Channel F cart layout on a built image.

The BIOS jumps to $0802 only if $0800 holds $55 (from disassembling
sl31253/sl31254); otherwise it runs its built-in Hockey.
"""
import sys

ROM_BASE = 0x0800
SIG = 0x55
ROM_SIZE = 0x4000          # FN_ROM_SIZE: a FujiNet client is exactly this
CLAIM_OFF = ROM_SIZE - 4   # FN_ROM_CLAIM
CLAIM = b"FUJI"
PAGE = 0x2000              # FN_APP_PAGE: APPBANK pages follow the 16K
MAX_PAGES = 6              # FN_APP_MAX_PAGES


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}
    if not args:
        sys.exit("usage: checkrom.py <image.bin> [expected-size] [--claim|--banked]")
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

    # A short client, or one that forgot the ORG, would otherwise boot with the
    # mailbox dead.
    if "--claim" in flags:
        if len(img) != ROM_SIZE:
            errs.append("a client image must be exactly %d bytes, got %d"
                        % (ROM_SIZE, len(img)))
        elif img[CLAIM_OFF:CLAIM_OFF + 4] != CLAIM:
            errs.append("no %r claim at $%04X (found %r); the mailbox would go "
                        "dead at boot" % (CLAIM.decode(), ROM_BASE + CLAIM_OFF,
                                          bytes(img[CLAIM_OFF:CLAIM_OFF + 4])))

    # The cart refuses a ragged banked image (TOOBIG) or one without the claim
    # (NOMAP) only at boot, so catch them here.
    if "--banked" in flags:
        extra = len(img) - ROM_SIZE
        if extra <= 0 or extra % PAGE or extra // PAGE > MAX_PAGES:
            errs.append("a banked image must be 16K + 1..%d whole 8K pages, got %d bytes"
                        % (MAX_PAGES, len(img)))
        if img[CLAIM_OFF:CLAIM_OFF + 4] != CLAIM:
            errs.append("no %r claim at $%04X; the cart refuses a banked image "
                        "without it" % (CLAIM.decode(), ROM_BASE + CLAIM_OFF))

    if errs:
        for e in errs:
            print("checkrom: %s: %s" % (path, e), file=sys.stderr)
        return 1
    if "--banked" in flags:
        print("checkrom: %s: %d bytes = 16K + %d pages, signature ok, claim ok"
              % (path, len(img), (len(img) - ROM_SIZE) // PAGE))
        return 0
    print("checkrom: %s: %d bytes, $%04X-$%04X, signature ok%s"
          % (path, len(img), ROM_BASE, ROM_BASE + len(img) - 1,
             ", claim ok" if "--claim" in flags else ""))
    return 0


sys.exit(main(sys.argv))
