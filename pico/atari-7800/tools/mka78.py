#!/usr/bin/env python3
"""mka78.py -- put the 128-byte .a78 header MAME needs on a headerless image.

    tools/mka78.py image.bin out.a78 [title]

MAME loads a loose 7800 cart only with this header; the FujiNet cart strips
it. Cart type 0 (plain ROM), one joystick per port, NTSC.
"""
import sys

if len(sys.argv) < 3:
    sys.exit("usage: mka78.py image.bin out.a78 [title]")
data = open(sys.argv[1], "rb").read()
title = (sys.argv[3] if len(sys.argv) > 3 else "FUJINET").encode()[:32]
h = bytearray(128)
h[0] = 1
h[1:10] = b"ATARI7800"
h[17:17 + len(title)] = title
h[49:53] = len(data).to_bytes(4, "big")
h[53:55] = (0).to_bytes(2, "big")
h[55] = 1
h[56] = 1
h[57] = 0
h[100:128] = b"ACTUAL CART DATA STARTS HERE"
open(sys.argv[2], "wb").write(bytes(h) + data)
