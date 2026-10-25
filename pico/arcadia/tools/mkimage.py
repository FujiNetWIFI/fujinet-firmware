#!/usr/bin/env python3
"""mkimage.py -- splice an assembled $0000-$2FFF flat into an 8K cart image.

The 2650 sees the cartridge as two 4K blocks: image 0x0000-0x0FFF at CPU
$0000 and image 0x1000-0x1FFF at CPU $2000 (A13 picks the block, A12 low is
the chip select -- see firmware/include/fuji_mailbox.h). The assembler works
in CPU addresses, so p2bin emits a flat covering $0000-$2FFF and this tool
folds it: image = flat[0:0x1000] + flat[0x2000:0x3000], zero-padded to 8K.

Two holes must be empty and are enforced here, not left to checkrom:
  - flat $1000-$1FFF is console RAM/UVI mirror space; code there is an ORG
    mistake and would silently vanish from the cart;
  - flat $2B00-$2FFF is the mailbox (reply/status/hotspot pages); a client
    that grows into it fails its build, not a debugging session.

An APPBANK image (protocol 2) adds --pages N: the flat then runs on past
$2FFF, page n (1..N) assembled at flat $3000 + (n-1) * $800 (PHASEd to the
$0800 window), and the image is the folded 8K plus those N pages.

Usage: mkimage.py [--pages N] flat.bin image.bin
"""

import sys

BLOCK = 0x1000
WINDOW = 8192
ROM_TOP2 = 0x0B00          # block-2 client budget: $2000-$2AFF


PAGE = 0x800
MAXPAGES = 31


def main() -> int:
    args = sys.argv[1:]
    pages = 0
    if len(args) == 4 and args[0] == "--pages":
        pages = int(args[1])
        args = args[2:]
        if not 1 <= pages <= MAXPAGES:
            print(f"mkimage: --pages must be 1-{MAXPAGES}", file=sys.stderr)
            return 2
    if len(args) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(args[0], "rb") as f:
        flat = f.read()
    top = 0x3000 + pages * PAGE
    if len(flat) > top:
        print(f"mkimage: flat is {len(flat)} bytes, past ${top - 1:04X}",
              file=sys.stderr)
        return 1
    flat = flat.ljust(top, b"\0")

    hole = flat[0x1000:0x2000]
    if hole.strip(b"\0"):
        off = 0x1000 + next(i for i, b in enumerate(hole) if b)
        print(f"mkimage: code or data at ${off:04X} -- console RAM/UVI mirror "
              f"space, not cartridge (ORG mistake?)", file=sys.stderr)
        return 1
    tail = flat[0x2000 + ROM_TOP2:0x3000]
    if tail.strip(b"\0"):
        off = 0x2000 + ROM_TOP2 + next(i for i, b in enumerate(tail) if b)
        print(f"mkimage: code or data at ${off:04X} -- inside the mailbox "
              f"pages ($2B00-$2FFF must stay clear)", file=sys.stderr)
        return 1

    image = (flat[0:BLOCK] + flat[0x2000:0x3000]).ljust(WINDOW, b"\0")
    image += flat[0x3000:top]
    with open(args[1], "wb") as f:
        f.write(image)
    return 0


if __name__ == "__main__":
    sys.exit(main())
