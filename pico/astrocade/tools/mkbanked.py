#!/usr/bin/env python3
"""mkbanked.py -- turn a claimed 8K client into an APPBANK image.

Usage:
  mkbanked.py client.bin out.bin --pages TOTAL [--entry 0x3000]
  mkbanked.py --verify image.bin [image2.bin ...]

The image is the 8K window (pages 0 and 1) plus stamped 4K pages
(fuji_mailbox.h, "banked images"). Each appended page carries:
  +00   the 7-byte sentinel header (55H, DW menu-link, DW name, DW start)
        whose start vector is the client's reset entry -- a console RESET
        with any page selected walks this header and lands in high-half
        code that re-selects page 0;
  +07   the page number, +08 its complement (the client's check marker);
  +0EH  the menu-visible name;
  +20H  PRNG fill, x' = (5x + 47H) & FFH, seeded page ^ A5H.
The total page count is stamped into page 0 at offset 0FF0H (console
2FF0H), which must be zero in the input client.
"""

import sys

WINDOW = 8192
PAGE = 4096
MAX_PAGES = 112
CLAIM_OFF = 0x1CFC
CLAIM_SIG = b"FUJI"
NPG_OFF = 0x0FF0
MARK_OFF = 0x07
NAME_OFF = 0x0E
PRNG_OFF = 0x20
MENUST = 0x0218
DEFAULT_ENTRY = 0x3000
PAGE_NAME = b"FUJINET BANK\0"


def prng(seed: int, n: int) -> bytes:
    x = seed
    out = bytearray()
    for _ in range(n):
        x = (5 * x + 0x47) & 0xFF
        out.append(x)
    return bytes(out)


def stamp_page(page: int, entry: int) -> bytes:
    b = bytearray(PAGE)
    b[0] = 0x55
    b[1:3] = MENUST.to_bytes(2, "little")
    b[3:5] = (0x2000 + NAME_OFF).to_bytes(2, "little")
    b[5:7] = entry.to_bytes(2, "little")
    b[MARK_OFF] = page
    b[MARK_OFF + 1] = page ^ 0xFF
    b[NAME_OFF:NAME_OFF + len(PAGE_NAME)] = PAGE_NAME
    b[PRNG_OFF:] = prng(page ^ 0xA5, PAGE - PRNG_OFF)
    return bytes(b)


def build(client: bytes, total: int, entry: int) -> bytes:
    if len(client) != WINDOW:
        sys.exit(f"mkbanked: client is {len(client)} bytes, must be {WINDOW}")
    if client[0] != 0x55:
        sys.exit("mkbanked: client lacks the 0x55 sentinel")
    if client[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] != CLAIM_SIG:
        sys.exit("mkbanked: client lacks the FUJI claim -- an unclaimed "
                 "image can never be an APPBANK")
    if client[NPG_OFF] != 0:
        sys.exit(f"mkbanked: page-count cell {NPG_OFF:#06x} is not free")
    if not 3 <= total <= MAX_PAGES:
        sys.exit(f"mkbanked: --pages {total} out of range (3-{MAX_PAGES})")
    out = bytearray(client)
    out[NPG_OFF] = total
    for page in range(2, total):
        out += stamp_page(page, entry)
    return bytes(out)


def verify(path: str) -> list[str]:
    problems = []
    with open(path, "rb") as f:
        img = f.read()
    if len(img) < WINDOW + PAGE or len(img) % PAGE != 0:
        return [f"size {len(img)} is not 8K + k*4K"]
    total = len(img) // PAGE
    if total > MAX_PAGES:
        problems.append(f"{total} pages, over the {MAX_PAGES} op range")
    if img[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] != CLAIM_SIG:
        problems.append("claim signature missing")
    if img[NPG_OFF] != total:
        problems.append(f"page-count cell says {img[NPG_OFF]}, image has {total}")
    for page in range(2, total):
        b = img[page * PAGE:(page + 1) * PAGE]
        if b[0] != 0x55:
            problems.append(f"page {page}: no sentinel header")
        if b[MARK_OFF] != page or b[MARK_OFF + 1] != (page ^ 0xFF):
            problems.append(f"page {page}: marker pair wrong")
        if b[PRNG_OFF:] != prng(page ^ 0xA5, PAGE - PRNG_OFF):
            problems.append(f"page {page}: PRNG fill wrong")
    return problems


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] == "--verify":
        bad = 0
        for path in args[1:]:
            problems = verify(path)
            if problems:
                bad = 1
                for p in problems:
                    print(f"mkbanked: {path}: {p}", file=sys.stderr)
            else:
                print(f"mkbanked: {path}: ok")
        return bad

    if len(args) < 4 or args[2] != "--pages":
        print(__doc__.strip(), file=sys.stderr)
        return 2
    total = int(args[3], 0)
    entry = DEFAULT_ENTRY
    if len(args) >= 6 and args[4] == "--entry":
        entry = int(args[5], 0)
    with open(args[0], "rb") as f:
        client = f.read()
    out = build(client, total, entry)
    with open(args[1], "wb") as f:
        f.write(out)
    print(f"mkbanked: {args[1]}: {total} pages, {len(out)} bytes, "
          f"entry {entry:#06x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
