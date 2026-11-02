#!/usr/bin/env python3
"""checkrom.py -- check (and stamp) a built ColecoVision FujiNet client against the layout
in firmware/include/fuji_mailbox.h:
  - exactly 32768 bytes;
  - the header magic at $8000: 55 AA (skip the title screen) or AA 55;
  - nothing but filler in 0x7800-0x7FFF but the "FUJI" claim at 0x7CFC, so no code or
    data, and so no NMI handler, sits in the mailbox pages;
  - the claim present. --stamp writes it first.

Usage: checkrom.py [--stamp] image.bin [image2.bin ...]
"""

import sys

WINDOW = 0x8000
ROM_TOP = 0x7800
CLAIM_OFF = 0x7CFC
CLAIM_SIG = b"FUJI"
FILLER = {0x00, 0xFF}


def stamp(path: str) -> None:
    with open(path, "r+b") as rom:
        rom.seek(CLAIM_OFF)
        rom.write(CLAIM_SIG)


def check(path: str) -> list[str]:
    with open(path, "rb") as rom:
        img = rom.read()
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

    status = 0
    for path in args:
        if do_stamp:
            stamp(path)
        problems = check(path)
        if problems:
            status = 1
            for problem in problems:
                print(f"{path}: {problem}", file=sys.stderr)
        else:
            print(f"{path}: ok")
    return status


if __name__ == "__main__":
    sys.exit(main())
