#!/usr/bin/env python3
"""expect.py -- what the console should see in its 32K window at boot.

Deliberately a separate, independent implementation of colmap's boot-state
mapping rather than a call into it: if the cartridge and the expectation came
from the same code, the soak would only prove that code is self-consistent.

Usage: expect.py image.bin out.bin
"""

import sys

WINDOW = 0x8000


def expect(img: bytes) -> bytes:
    size = len(img)

    if size <= WINDOW:
        # A cartridge only wires the chip selects its own size needs, so
        # everything past the end of the image is open bus.
        return img + b"\xff" * (WINDOW - size)

    banks = size >> 14
    if size in (0x100000, 0x200000):
        # X-in-1: the LAST 32K window at reset.
        return img[size - WINDOW:]
    if banks > 2 and (banks & (banks - 1)) == 0 and banks <= 32:
        # MegaCart: low 16K pinned to the last bank, high 16K to bank 0.
        return img[(banks - 1) * 0x4000:banks * 0x4000] + img[0:0x4000]
    raise SystemExit(f"expect.py: no mapper fits {size} bytes")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    img = open(sys.argv[1], "rb").read()
    out = expect(img)
    assert len(out) == WINDOW
    open(sys.argv[2], "wb").write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
