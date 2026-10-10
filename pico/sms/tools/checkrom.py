#!/usr/bin/env python3
"""checkrom.py -- check (and with --stamp, claim) an SMS FujiNet client image.

    checkrom.py [--stamp] in.sms [out.sms]

A client is whole 16K banks, at least 32K, with appmake's SEGA header at
$7FF0. --stamp writes "FUJI" at $7FDC and then re-stamps the header checksum,
since the claim sits inside the range the export BIOS sums ($0000-$7FEF).
Code must leave $7FD8-$7FEF as filler, and every bank from 2 up must leave its
last 4K as filler: that is where the cart's arena sits over slot 2.
"""

import sys

CLAIM_OFF = 0x7FDC
FENCE = (0x7FD8, 0x7FF0)
HEADER = 0x7FF0
ARENA_IN_BANK = (0x3000, 0x4000)


def checksum(img):
    return sum(img[0:0x7FF0]) & 0xFFFF


def fail(msg):
    sys.exit("checkrom: %s" % msg)


def main():
    args = sys.argv[1:]
    stamp = "--stamp" in args
    args = [a for a in args if a != "--stamp"]
    if not args:
        sys.exit(__doc__)
    src = args[0]
    dst = args[1] if len(args) > 1 else src
    img = bytearray(open(src, "rb").read())

    if len(img) < 0x8000 or len(img) % 0x4000:
        fail("%s is %d bytes: not whole 16K banks of at least 32K" % (src, len(img)))
    if img[HEADER:HEADER + 8] != b"TMR SEGA":
        fail("%s has no SEGA header at $7FF0 (code past $7FF0?)" % src)
    if img[0x7FFF] >> 4 != 4:
        fail("%s: region nibble %d, want 4 (export)" % (src, img[0x7FFF] >> 4))
    have = img[0x7FFA] | img[0x7FFB] << 8

    if stamp:
        if checksum(img) != have:
            fail("%s: appmake's checksum %04X does not match %04X" % (src, have, checksum(img)))
        if any(b != 0xFF for b in img[FENCE[0]:FENCE[1]]):
            fail("%s: code reaches $%04X, the claim's fence" % (src, FENCE[0]))
        img[CLAIM_OFF:CLAIM_OFF + 4] = b"FUJI"
        s = checksum(img)
        img[0x7FFA] = s & 0xFF
        img[0x7FFB] = s >> 8
    elif img[CLAIM_OFF:CLAIM_OFF + 4] != b"FUJI":
        fail("%s carries no claim at $7FDC" % src)
    elif checksum(img) != have:
        fail("%s: header checksum %04X, image sums to %04X" % (src, have, checksum(img)))

    for bank in range(2, len(img) // 0x4000):
        lo = bank * 0x4000 + ARENA_IN_BANK[0]
        hi = bank * 0x4000 + ARENA_IN_BANK[1]
        if any(b != 0xFF for b in img[lo:hi]):
            fail("%s: bank %d uses $B000-$BFFF, which the arena covers" % (src, bank))

    open(dst, "wb").write(img)
    print("checkrom: %s ok (%dK, claim at $7FDC, checksum %04X)"
          % (dst, len(img) // 1024, img[0x7FFA] | img[0x7FFB] << 8))


if __name__ == "__main__":
    main()
