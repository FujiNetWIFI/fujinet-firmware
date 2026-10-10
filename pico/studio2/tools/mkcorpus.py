#!/usr/bin/env python3
"""Synthetic ST2 images for the page layouts the TOSEC corpus never uses.

    tools/mkcorpus.py build/obj/corpus.p build/corpus

Each holds testrom/corpus.asm at $0400 (it draws what $0A00-$0F00 read) and
distinct data in the pages its name gives. Where MAME's loader and the cart
must agree, Tier A compares them; where they differ by design (MAME's
sub-$04 misalignment, pages it never maps) the README says so.
"""
import os
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import st2  # noqa: E402
from mkst2 import read_p  # noqa: E402

LAYOUTS = {
    "c0c":      [0x0C],                 # MAME maps $0C00-$0FFF, $0D-$0F zero
    "c0c0f":    [0x0C, 0x0D, 0x0E, 0x0F],
    "c0d0f":    [0x0C, 0x0F],           # a hole at $0D-$0E
    "max11":    [0x05, 0x06, 0x07, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F],
    "only0d":   [0x0D],                 # no $0C: MAME never maps it (differs)
    "only0a":   [0x0A],                 # MAME never maps $0A00 (differs)
    "sub04":    [0x02, 0x0C],           # MAME misreads after a sub-$04 block (differs)
}


def main():
    code = read_p(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    page4 = bytearray(256)
    for a, b in code.items():
        page4[a - 0x400] = b
    for name, extra in LAYOUTS.items():
        pages = {0x04: bytes(page4)}
        order = [0x04] + extra
        for i, pg in enumerate(extra):
            pages[pg] = bytes(((pg * 37 + k * 11 + i * 5) & 0xFF) | 0x11 for k in range(256))
        if name == "sub04":
            # st2.build sorts pages; write this one by hand, block order kept
            hdr = bytearray(256)
            hdr[0:4] = b"RCA2"
            hdr[4] = len(order) + 1
            hdr[5] = 1
            hdr[32:32 + len(name)] = name.upper().encode()
            for i, pg in enumerate([0x04, 0x02, 0x0C]):
                hdr[64 + i] = pg
            body = page4 + pages[0x02] + pages[0x0C]
            data = bytes(hdr) + bytes(body)
        else:
            data = st2.build(pages, name.upper())
        open(os.path.join(out, name + ".st2"), "wb").write(data)
    print("mkcorpus: %d images in %s" % (len(LAYOUTS), out))


if __name__ == "__main__":
    main()
