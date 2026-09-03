#!/usr/bin/env python3
"""checkrom.py -- validate a built Astrocade FujiNet client image.

Enforces the layout contract in firmware/include/fuji_mailbox.h so a client
that drifts into the mailbox pages fails its build, not a debugging session:
  - exactly 8192 bytes;
  - the 0x55 sentinel first;
  - nothing but zeros in 0x1B00-0x1FFF except the "FUJI" claim at 0x1CFC;
  - the claim signature present.
Usage: checkrom.py image.bin [image2.bin ...]
"""

import sys

WINDOW = 8192
SENTINEL = 0x55
ROM_TOP = 0x1B00
CLAIM_OFF = 0x1CFC
CLAIM_SIG = b"FUJI"


def check(path: str) -> list[str]:
    problems = []
    with open(path, "rb") as f:
        img = f.read()
    if len(img) != WINDOW:
        problems.append(f"size is {len(img)}, must be exactly {WINDOW}")
        return problems
    if img[0] != SENTINEL:
        problems.append(f"first byte is {img[0]:#04x}, not the 0x55 sentinel")
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
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    bad = 0
    for path in sys.argv[1:]:
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
