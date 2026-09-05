#!/usr/bin/env python3
"""checkrom.py -- validate a built Arcadia FujiNet client image.

Enforces the layout contract in firmware/include/fuji_mailbox.h so a client
that drifts into the mailbox pages fails its build, not a debugging session:
  - exactly 8192 bytes;
  - the cartridge header: a BCTA,UN ($1F) at image 0 and the RETC,UN ($17)
    interrupt guard at image 3 (the convention every commercial cart follows;
    some consoles vector a sprite interrupt through $0003);
  - nothing but zeros in 0x1B00-0x1FFF except the "FUJI" claim at 0x1CFC;
  - the claim signature present.
Usage: checkrom.py image.bin [image2.bin ...]
"""

import sys

WINDOW = 8192
ROM_TOP = 0x1B00
CLAIM_OFF = 0x1CFC
CLAIM_SIG = b"FUJI"


def check(path: str) -> list[str]:
    with open(path, "rb") as f:
        img = f.read()
    if len(img) != WINDOW:
        return [f"size is {len(img)}, must be exactly {WINDOW}"]
    problems = []
    if img[0] != 0x1F:
        problems.append(f"first byte is {img[0]:#04x}, not a BCTA,UN header")
    if img[3] != 0x17:
        problems.append(f"byte 3 is {img[3]:#04x}, not the $17 RETC,UN "
                        f"interrupt guard")
    if img[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] != CLAIM_SIG:
        problems.append("claim signature 'FUJI' missing at 0x1CFC")
    for off in range(ROM_TOP, WINDOW):
        if CLAIM_OFF <= off < CLAIM_OFF + len(CLAIM_SIG):
            continue
        if img[off] != 0:
            problems.append(
                f"code or data at {off:#06x}, above the 0x1AFF ROM top "
                f"(first offender; mailbox pages must stay clear)")
            break
    return problems


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    bad = 0
    for path in args:
        problems = check(path)
        if problems:
            bad = 1
            for p in problems:
                print(f"checkrom: {path}: {p}", file=sys.stderr)
        else:
            print(f"checkrom: {path}: ok")
    return bad


if __name__ == "__main__":
    sys.exit(main())
