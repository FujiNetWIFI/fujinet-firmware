#!/usr/bin/env python3
"""contact.py -- a contact sheet of run snapshots, for review.

    contact.py OUT.png ID:LABEL=PNG [ID:LABEL=PNG ...]
    contact.py --results results.tsv --shots DIR [--only VERDICT,...] OUTPREFIX

The second form lays out every image in a results file, 6 x 6 per sheet.
"""

import os
import sys

from PIL import Image, ImageDraw

W, H, COLS, ROWS = 268, 224, 6, 6


def sheet(items, out):
    img = Image.new("RGB", (COLS * W, ROWS * (H + 14)), "white")
    d = ImageDraw.Draw(img)
    for i, (label, path) in enumerate(items):
        x, y = (i % COLS) * W, (i // COLS) * (H + 14)
        if path and os.path.exists(path):
            img.paste(Image.open(path).convert("RGB").resize((W, H)), (x, y + 14))
        d.text((x + 2, y + 1), label[:44], fill="black")
    img.save(out)
    print("contact: %s (%d)" % (out, len(items)))


def main():
    a = sys.argv[1:]
    if a and a[0] == "--results":
        res, shots = a[1], a[3]
        only = None
        if "--only" in a:
            only = set(a[a.index("--only") + 1].split(","))
        prefix = a[-1]
        items = []
        for line in open(res):
            rid, name, verdict = line.split("\t")[:3]
            if only and verdict.split("@")[0] not in only:
                continue
            items.append(("%s %s %s" % (rid, verdict, name), os.path.join(shots, rid + ".png")))
        for n in range(0, len(items), COLS * ROWS):
            sheet(items[n:n + COLS * ROWS], "%s-%02d.png" % (prefix, n // (COLS * ROWS)))
        return
    out = a[0]
    sheet([(x.split("=", 1)[0], x.split("=", 1)[1]) for x in a[1:]], out)


if __name__ == "__main__":
    main()
