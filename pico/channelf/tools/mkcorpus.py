#!/usr/bin/env python3
"""Build a corpus of Videocart images for the soak.

Synthetic images sized to every real Videocart size and boundary (one byte,
one short of the window, exactly the window), with distinct contents so a
stale window cannot pass. Banked clients are 16K plus whole 8K pages, always
claimed. Two must be refused, as their names say: a banked image without the
claim (NOMAP), and one byte past a page (TOOBIG).
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
PAGE = 0x2000
BANKED = [
    ("bank1", WINDOW + PAGE),          # 24K: one page
    ("bank3", WINDOW + 3 * PAGE),      # 40K
    ("bank6", WINDOW + 6 * PAGE),      # 64K: the largest
]
# name, size, claimed, the FN_BOOT_ERR_* the cart must refuse it with
REFUSED = [
    ("xfail_nomap", WINDOW + PAGE, False, 3),
    ("xfail_toobig", WINDOW + PAGE + 1, True, 1),
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
        # One 16K variant carries the claim, so both mailbox outcomes are soaked.
        if size == WINDOW and variant == "b":
            img[WINDOW - 4:WINDOW] = b"FUJI"
        p = out / f"soak_{name}_{variant}.bin"
        p.write_bytes(img)
        made.append((p, size))

def banked(size, seed, claim):
    img = bytearray(size)
    for i in range(size):
        # the page number folded in, so serving the wrong page cannot match
        img[i] = (i * 29 + seed * 11 + (i >> 8) + 3 * (i >> 13)) & 0xFF
    img[0], img[1] = 0x55, 0x00
    if claim:
        img[WINDOW - 4:WINDOW] = b"FUJI"
    return img


for name, size in BANKED:
    p = out / f"soak_{name}.bin"
    p.write_bytes(banked(size, sum(name.encode()) & 0xFF, True))
    made.append((p, size))
for name, size, claim, err in REFUSED:
    p = out / f"soak_{name}_err{err}.bin"
    p.write_bytes(banked(size, sum(name.encode()) & 0xFF, claim))
    made.append((p, size))

print("mkcorpus: %d images in %s" % (len(made), out))
for p, size in made:
    print("  %-28s %6d bytes" % (p.name, size))
