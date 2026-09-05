#!/usr/bin/env python3
"""mkgame.py -- build a synthetic 256K/512K game-mapper image.

Usage:
  mkgame.py program4k.bin out.bin --size 256k|512k
  mkgame.py --verify image.bin [image2.bin ...]

The 4K program (gamebank.asm) becomes the LAST bank -- the one the mapper
fixes at console 2000H-2FFFH, so it is the menu entry the console boots.
Every other bank is stamped for it to verify through the switched window:
the bank number and its complement at +0/+1, then PRNG fill
(x' = (5x + 47H) & FFH, seeded bank ^ A5H).

--verify walks the finished image with a transcription of MAME's
rom_256k/rom_512k read handlers: every hotspot read must return its bank
number, every stamp must read back through the switched window, and the
fixed low half must byte-match the program.
"""

import sys

PAGE = 4096
SIZES = {"256k": (0x40000, 64, 0x1FC0, 0x3F),
         "512k": (0x80000, 128, 0x1F80, 0x7F)}
CLAIM_OFF = 0x1CFC
CLAIM_SIG = b"FUJI"


def prng(seed: int, n: int) -> bytes:
    x = seed
    out = bytearray()
    for _ in range(n):
        x = (5 * x + 0x47) & 0xFF
        out.append(x)
    return bytes(out)


def stamp_bank(bank: int) -> bytes:
    b = bytearray(PAGE)
    b[0] = bank
    b[1] = bank ^ 0xFF
    b[2:] = prng(bank ^ 0xA5, PAGE - 2)
    return bytes(b)


def build(program: bytes, size_key: str) -> bytes:
    size, nbanks, _, _ = SIZES[size_key]
    if len(program) > PAGE:
        sys.exit(f"mkgame: program is {len(program)} bytes, over 4K")
    if not program or program[0] != 0x55:
        sys.exit("mkgame: program lacks the 0x55 sentinel")
    program = program + bytes(PAGE - len(program))
    img = bytearray()
    for bank in range(nbanks - 1):
        img += stamp_bank(bank)
    img += program
    assert len(img) == size
    if img[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] == CLAIM_SIG:
        sys.exit("mkgame: stamp data spells the FUJI claim; refusing")
    return bytes(img)


class MameRef:
    """MAME rom.cpp read handlers, transcribed verbatim."""

    def __init__(self, img: bytes, nbanks: int):
        self.rom = img
        self.base_bank = 0
        self.hot = 0x1FC0 if nbanks == 64 else 0x1F80
        self.mask = nbanks - 1
        self.fixed = nbanks - 1

    def read(self, offset: int) -> int:
        if offset < 0x1000:
            return self.rom[offset + 0x1000 * self.fixed]
        if offset < self.hot:
            return self.rom[(offset & 0xFFF) + 0x1000 * self.base_bank]
        self.base_bank = offset & self.mask
        return self.base_bank


def verify(path: str) -> list[str]:
    with open(path, "rb") as f:
        img = f.read()
    for size, nbanks, hot, mask in SIZES.values():
        if len(img) == size:
            break
    else:
        return [f"size {len(img)} is neither 256K nor 512K"]
    problems = []
    ref = MameRef(img, nbanks)
    program = img[-PAGE:]
    for off in range(0, PAGE, 257):     # the fixed low half is the program
        if ref.read(off) != program[off]:
            problems.append(f"fixed half mismatch at {off:#06x}")
            break
    for bank in range(nbanks):
        got = ref.read(hot + bank)
        if got != bank:
            problems.append(f"hotspot {hot + bank:#06x} returned {got}")
            break
        window = bytes(ref.read(0x1000 + i) for i in range(64))
        want = program[:64] if bank == nbanks - 1 else stamp_bank(bank)[:64]
        if window != want:
            problems.append(f"bank {bank}: switched window mismatch")
            break
    if img[CLAIM_OFF:CLAIM_OFF + len(CLAIM_SIG)] == CLAIM_SIG:
        problems.append("image spells the FUJI claim")
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
                    print(f"mkgame: {path}: {p}", file=sys.stderr)
            else:
                print(f"mkgame: {path}: ok")
        return bad

    if len(args) != 4 or args[2] != "--size" or args[3] not in SIZES:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(args[0], "rb") as f:
        program = f.read()
    out = build(program, args[3])
    with open(args[1], "wb") as f:
        f.write(out)
    print(f"mkgame: {args[1]}: {len(out)} bytes, "
          f"{SIZES[args[3]][1]} banks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
