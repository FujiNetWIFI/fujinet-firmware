#!/usr/bin/env python3
"""Turn an AS object file (.p) into the ST2 image the cart serves.

    tools/mkst2.py build/obj/hello.p build/hello.st2 [title] [--bank b1.p ...]

Every page the code touches becomes a block, plus $04-$07 (every cart has
them); each must be a page the cart may claim (tools/st2.py). A client with
"FUJI" at $07FC is an app and is held to the app's rules. Each --bank object
adds a bank (1, 2, ...) of the $C400-$CFFF window: only its window pages are
taken, so it may be the same program assembled again for that bank.
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


def to_pages(mem):
    pages = {}
    for a, b in mem.items():
        pages.setdefault(a >> 8, bytearray(256))[a & 0xFF] = b
    return pages


def main():
    args = sys.argv[1:]
    bankp = []
    while "--bank" in args:
        i = args.index("--bank")
        bankp.append(args[i + 1])
        del args[i:i + 2]
    if len(args) < 2:
        raise SystemExit("usage: mkst2.py <in.p> <out.st2> [title] [--bank b.p ...]")
    mem = read_p(args[0])
    title = args[2] if len(args) > 2 else os.path.basename(args[1]).split(".")[0].upper()
    pages = to_pages(mem)
    banks = [{pg: d for pg, d in to_pages(read_p(b)).items() if st2.in_window(pg)} for b in bankp]
    for pg in range(0x04, 0x08):
        pages.setdefault(pg, bytearray(256))
    claim = bytes(pages[0x07][0xFC:0x100]) == b"FUJI"
    bad = sorted(p for p in pages if not st2.page_ok(p, app=claim))
    if bad:
        raise SystemExit("%s: code in pages the cart may not claim%s: %s"
                         % (args[0], " for an app" if claim else "",
                            " ".join("$%02X" % p for p in bad)))
    out = st2.build(pages, title, banks=banks)
    open(args[1], "wb").write(out)
    nb = sum(len(b) for b in banks)
    print("%s: %d bytes in %d pages%s%s" % (args[1], len(mem), len(pages),
          ", %d banks (+%d pages)" % (1 + len(banks), nb) if banks else "",
          ", claimed" if claim else ""))


if __name__ == "__main__":
    main()
