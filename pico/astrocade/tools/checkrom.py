#!/usr/bin/env python3
"""checkrom.py -- validate a built Astrocade FujiNet client image.

Enforces the layout contract in firmware/include/fuji_mailbox.h so a client
that drifts into the mailbox pages fails its build, not a debugging session.
Default mode, an ordinary 8K client:
  - exactly 8192 bytes;
  - the 0x55 sentinel first;
  - nothing but zeros in 0x1B00-0x1FFF except the "FUJI" claim at 0x1CFC;
  - the claim signature present.
--banked: an APPBANK image -- the same rules on the first 8K, a size of
8K + k*4K within the page-op range, and a sentinel opening every appended
page (the console-RESET escape hatch).
--game N (256 or 512): a synthetic game image -- exact size, the sentinel
at the top of the LAST bank (the fixed half), and NO claim signature.
Usage: checkrom.py [--banked | --game N] image.bin [image2.bin ...]
"""

import sys

WINDOW = 8192
PAGE = 4096
MAX_PAGES = 112
SENTINEL = 0x55
ROM_TOP = 0x1B00
CLAIM_OFF = 0x1CFC
CLAIM_SIG = b"FUJI"
GAME_SIZES = {"256": 0x40000, "512": 0x80000}


def check_window(img: bytes) -> list[str]:
    problems = []
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


def check(path: str) -> list[str]:
    with open(path, "rb") as f:
        img = f.read()
    if len(img) != WINDOW:
        return [f"size is {len(img)}, must be exactly {WINDOW}"]
    return check_window(img)


def check_banked(path: str) -> list[str]:
    with open(path, "rb") as f:
        img = f.read()
    if len(img) < WINDOW + PAGE or len(img) % PAGE != 0:
        return [f"size is {len(img)}, must be 8K + k*4K"]
    total = len(img) // PAGE
    problems = []
    if total > MAX_PAGES:
        problems.append(f"{total} pages, over the {MAX_PAGES} op range")
    problems += check_window(img[:WINDOW])
    for page in range(2, total):
        if img[page * PAGE] != SENTINEL:
            problems.append(
                f"page {page} does not open with the 0x55 sentinel header "
                f"(console RESET with it selected would strand the machine)")
            break
    return problems


def check_game(path: str, variant: str) -> list[str]:
    with open(path, "rb") as f:
        img = f.read()
    want = GAME_SIZES[variant]
    if len(img) != want:
        return [f"size is {len(img)}, must be exactly {want:#x}"]
    problems = []
    if img[-PAGE] != SENTINEL:
        problems.append("last bank (the fixed half) lacks the 0x55 sentinel")
    if img[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] == CLAIM_SIG:
        problems.append("carries the FUJI claim: would map as an APPBANK")
    return problems


def main() -> int:
    args = sys.argv[1:]
    mode = check
    if args and args[0] == "--banked":
        mode = check_banked
        args = args[1:]
    elif args and args[0] == "--game":
        if len(args) < 2 or args[1] not in GAME_SIZES:
            print(__doc__.strip(), file=sys.stderr)
            return 2
        variant = args[1]
        mode = lambda p: check_game(p, variant)
        args = args[2:]
    if not args:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    bad = 0
    for path in args:
        problems = mode(path)
        if problems:
            bad = 1
            for p in problems:
                print(f"checkrom: {path}: {p}", file=sys.stderr)
        else:
            print(f"checkrom: {path}: ok")
    return bad


if __name__ == "__main__":
    sys.exit(main())
