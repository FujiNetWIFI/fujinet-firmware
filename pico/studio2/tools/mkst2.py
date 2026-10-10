#!/usr/bin/env python3
"""Turn an AS object file (.p) into the ST2 image the cart serves.

    tools/mkst2.py build/obj/hello.p build/hello.st2 [title]

Every page the code touches becomes a block, plus $04-$07 (every cart has
them); each must be a page the cart may claim (tools/st2.py). A client with
"FUJI" at $07FC is an app and is held to the app's rules.
"""
import os
import struct
import sys

sys.dont_write_bytecode = True                  # no __pycache__ in the tree
sys.path.insert(0, os.path.dirname(__file__))
import st2  # noqa: E402


def read_p(path):
    """{address: byte} from an AS P-format file."""
    data = open(path, "rb").read()
    if struct.unpack_from("<H", data, 0)[0] != 0x1489:
        raise SystemExit("%s: not an AS .p file" % path)
    mem, pos = {}, 2
    while pos < len(data):
        hdr = data[pos]
        pos += 1
        if hdr == 0x00:                         # end: creator string follows
            break
        if hdr == 0x80:                         # entry point
            pos += 4
            continue
        if hdr == 0x81:                         # header, segment, granularity
            pos += 3
        start, length = struct.unpack_from("<IH", data, pos)
        pos += 6
        for i in range(length):
            if start + i in mem:                # AS lets an ORG overlap code
                raise SystemExit("%s: two things at $%04X" % (path, start + i))
            mem[start + i] = data[pos + i]
        pos += length
    return mem


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: mkst2.py <in.p> <out.st2> [title]")
    mem = read_p(sys.argv[1])
    title = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(sys.argv[2]).split(".")[0].upper()
    pages = {}
    for a, b in mem.items():
        pg = a >> 8
        pages.setdefault(pg, bytearray(256))[a & 0xFF] = b
    for pg in range(0x04, 0x08):
        pages.setdefault(pg, bytearray(256))
    claim = bytes(pages[0x07][0xFC:0x100]) == b"FUJI"
    bad = sorted(p for p in pages if not st2.page_ok(p, app=claim))
    if bad:
        raise SystemExit("%s: code in pages the cart may not claim%s: %s"
                         % (sys.argv[1], " for an app" if claim else "",
                            " ".join("$%02X" % p for p in bad)))
    out = st2.build(pages, title)
    open(sys.argv[2], "wb").write(out)
    used = len(mem)
    print("%s: %d bytes in %d pages%s" % (sys.argv[2], used, len(pages), ", claimed" if claim else ""))


if __name__ == "__main__":
    main()
