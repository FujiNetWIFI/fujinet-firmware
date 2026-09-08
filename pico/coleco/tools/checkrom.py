#!/usr/bin/env python3
"""checkrom.py -- validate (and stamp) a built ColecoVision FujiNet client.

Enforces the layout contract in firmware/include/fuji_mailbox.h so a client
that drifts into the mailbox pages fails its build, not a debugging session:
  - exactly 32768 bytes (z88dk's appmake pads to --romsize with 0xFF, and 0xFF
    is also what an unmapped cartridge address reads as, so a FujiNet image and
    a stock cart agree on the filler byte);
  - the cartridge header magic at $8000: 55 AA to skip the BIOS title screen,
    AA 55 to show it. z88dk emits 55 AA, which is what a CONFIG client wants;
  - nothing but filler in 0x7800-0x7FFF except the "FUJI" claim at 0x7CFC --
    those pages are the reply window, the status page and the three hotspot
    pages, and the client must never place code or data there. This is also
    what makes the client-side NMI rule ("never read $F800 and up from the
    vblank handler") checkable at build time rather than at 3am;
  - the claim signature present.

With --stamp the claim is written in first, so the same tool that checks the
layout is the one that declares it.

Usage: checkrom.py [--stamp] image.bin [image2.bin ...]
"""

import sys

WINDOW = 0x8000
ROM_TOP = 0x7800
CLAIM_OFF = 0x7CFC
CLAIM_SIG = b"FUJI"
FILLER = {0x00, 0xFF}


def stamp(path: str) -> None:
    with open(path, "r+b") as f:
        f.seek(CLAIM_OFF)
        f.write(CLAIM_SIG)


def check(path: str) -> list[str]:
    with open(path, "rb") as f:
        img = f.read()
    if len(img) != WINDOW:
        return [f"size is {len(img)}, must be exactly {WINDOW}"]

    problems = []
    magic = img[0:2]
    if magic not in (b"\x55\xaa", b"\xaa\x55"):
        problems.append(f"header magic is {magic.hex()}, not 55aa or aa55")
    if img[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] != CLAIM_SIG:
        problems.append(f"claim signature 'FUJI' missing at {CLAIM_OFF:#06x}")
    for off in range(ROM_TOP, WINDOW):
        if CLAIM_OFF <= off < CLAIM_OFF + len(CLAIM_SIG):
            continue
        if img[off] not in FILLER:
            problems.append(
                f"code or data at {off:#06x} ({off + 0x8000:#06x} to the "
                f"console), above the {ROM_TOP - 1 + 0x8000:#06x} ROM top; the "
                f"mailbox pages must stay clear (first offender)")
            break
    return problems


def main() -> int:
    args = sys.argv[1:]
    do_stamp = False
    if args and args[0] == "--stamp":
        do_stamp = True
        args = args[1:]
    if not args:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    rc = 0
    for path in args:
        if do_stamp:
            stamp(path)
        problems = check(path)
        if problems:
            rc = 1
            for p in problems:
                print(f"{path}: {p}", file=sys.stderr)
        else:
            print(f"{path}: ok")
    return rc


if __name__ == "__main__":
    sys.exit(main())
