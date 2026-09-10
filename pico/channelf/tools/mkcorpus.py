#!/usr/bin/env python3
"""Build a corpus of Videocart images for the soak.

There is no Channel F ROM set on this machine -- MAME ships only the BIOS -- so
the soak drives synthetic images instead. That is a weaker test of real-cart
compatibility and a stronger one of the push/stage/swap/serve path: the sizes
here are chosen to hit every boundary that matters (every real Videocart size,
one byte, one short of the window, exactly the window), and the contents are
distinct per image so a stale window cannot pass by looking plausible.
"""
import os
import pathlib
import sys

WINDOW = 0x4000
SIZES = [
    ("min", 1),
    ("1k", 1024),
    ("2k", 2048),        # the commonest Videocart
    ("3k", 3072),
    ("4k", 4096),
    ("6k", 6144),        # Saba Schach, the largest commercial cart
    ("8k", 8192),
    ("12k", 12288),
    ("win_1", WINDOW - 1),
    ("win", WINDOW),
]

out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/soak")
out.mkdir(parents=True, exist_ok=True)

made = []
for name, size in SIZES:
    for variant in ("a", "b"):
        img = bytearray(size)
        seed = (sum(name.encode()) + (0 if variant == "a" else 137)) & 0xFF
        for i in range(size):
            img[i] = (i * 31 + seed * 7 + (i >> 8)) & 0xFF
        img[0] = 0x55                      # the BIOS cart signature
        if size > 1:
            img[1] = 0x00
        # An exactly-full-window image gets the claim in one variant, so the
        # soak covers both "mailbox survives the boot" and "mailbox dies".
        if size == WINDOW and variant == "b":
            img[WINDOW - 4:WINDOW] = b"FUJI"
        p = out / f"soak_{name}_{variant}.bin"
        p.write_bytes(img)
        made.append((p, size))

print("mkcorpus: %d images in %s" % (len(made), out))
for p, size in made:
    print("  %-28s %6d bytes" % (p.name, size))
