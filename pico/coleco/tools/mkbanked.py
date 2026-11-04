#!/usr/bin/env python3
"""mkbanked.py -- stamp a flat 32K client into an APPBANK test image (fuji_mailbox.h).

Usage: mkbanked.py client.bin out.bin --pages N     (2 to 13)
       mkbanked.py --verify image.bin [...]

Page 0 is image 0x4000-0x5FFF, inside the 32K, and must be clear there; page n >= 1
is appended at 0x6000 + n * 0x2000. Every page: "PG", n, ~n, the page count (page 0
only), a PRNG fill, byte 0x1000 = n + 0x30, last byte n ^ 0x5A. Avoid 5 and 13 pages
(64K, 128K): older firmware takes those for a MegaCart.
"""

import sys

WINDOW = 0x8000
PAGE = 0x2000
PAGE0 = 0x4000
MAX_PAGES = 13
CLAIM_OFF = 0x7CFC
CLAIM_SIG = b"FUJI"


def page_bytes(page: int, count: int) -> bytes:
    data = bytearray(PAGE)
    seed = page ^ 0xA5
    for idx in range(8, PAGE):
        seed = (5 * seed + 0x47) & 0xFF
        data[idx] = seed
    data[0:4] = b"PG" + bytes([page, page ^ 0xFF])
    data[4] = count if page == 0 else 0
    data[0x1000] = (page + 0x30) & 0xFF
    data[PAGE - 1] = page ^ 0x5A
    return bytes(data)


def build(client: bytes, count: int) -> bytes:
    if len(client) != WINDOW:
        raise SystemExit(f"client is {len(client)} bytes, must be {WINDOW}")
    if not 2 <= count <= MAX_PAGES:
        raise SystemExit("--pages must be 2 to 13")
    if client[PAGE0:PAGE0 + PAGE].strip(b"\x00\xff"):
        raise SystemExit("client code or data reaches page 0 (image 0x4000-0x5FFF)")
    image = bytearray(client)
    image[PAGE0:PAGE0 + PAGE] = page_bytes(0, count)
    for page in range(1, count):
        image += page_bytes(page, count)
    return bytes(image)


def verify(path: str) -> list[str]:
    img = open(path, "rb").read()
    if len(img) <= WINDOW or (len(img) - WINDOW) % PAGE:
        return [f"size {len(img)} is not 32K plus whole 8K pages"]
    count = 1 + (len(img) - WINDOW) // PAGE
    problems = []
    if img[CLAIM_OFF:CLAIM_OFF + 4] != CLAIM_SIG:
        problems.append("no claim")
    for page in range(count):
        start = PAGE0 if page == 0 else 0x6000 + page * PAGE
        if img[start:start + PAGE] != page_bytes(page, count):
            problems.append(f"page {page} differs at image {start:#x}")
    return problems


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "--verify":
        status = 0
        for path in args[1:]:
            problems = verify(path)
            print(f"{path}: " + ("ok" if not problems else "; ".join(problems)))
            status |= bool(problems)
        return status
    if len(args) != 4 or args[2] != "--pages":
        print(__doc__.strip(), file=sys.stderr)
        return 2
    image = build(open(args[0], "rb").read(), int(args[3]))
    open(args[1], "wb").write(image)
    print(f"{args[1]}: {len(image)} bytes, {args[3]} pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
