#!/usr/bin/env python3
"""checkrom.py -- the rules an assembler cannot check, from an AS listing.

    tools/checkrom.py build/obj/hello.lst

- No 3-cycle instruction (LBR, the long skips, NOP: opcodes $C0-$CF): the
  display is on whenever a client runs, and one of them in flight when the
  1861 asks for DMA costs that line a byte. The ISR's one LBR, before the
  first DMA, is marked "!3CYCLE". No IDL either: it reads M(R0) every cycle
  it waits (CDP1802 Table 2), and R0 is on the raster.
- RF is the hotspot pointer: only LDN reads through it, and nothing stores
  or sets X with it (LDA/LDXA/STR/STXD/SEX/OUT on RF).
"""
import re
import sys

BANNED = {"IDL", "LBR", "LBQ", "LBZ", "LBDF", "LBNQ", "LBNZ", "LBNF", "LSKP", "LSNQ",
          "LSNZ", "LSNF", "LSIE", "LSQ", "LSZ", "LSDF", "NOP"}
RF_BAD = {"LDA", "STR", "SEX"}
LINE = re.compile(r"^(?:\(\d+\))?\s*\d+/\s*[0-9A-F]+ : (?:[0-9A-F]{2} )+\s*(.*)$")


def main():
    bad = []
    for n, raw in enumerate(open(sys.argv[1], errors="replace"), 1):
        m = LINE.match(raw.rstrip("\n"))
        if not m:
            continue
        src = m.group(1)
        code, _, comment = src.partition(";")
        toks = code.replace(":", ": ").split()
        if toks and toks[0].endswith(":"):
            toks = toks[1:]
        if not toks:
            continue
        op = toks[0].upper()
        arg = toks[1].upper().rstrip(",") if len(toks) > 1 else ""
        if op in BANNED and "!3CYCLE" not in comment:
            bad.append("line %d: %s -- a 3-cycle instruction (use FJMP/FCALL)" % (n, src.strip()))
        if op in RF_BAD and arg in ("RF", "15", "0FH"):
            bad.append("line %d: %s -- RF is only ever read with LDN" % (n, src.strip()))
    for b in bad:
        print("checkrom: %s: %s" % (sys.argv[1], b), file=sys.stderr)
    if not bad:
        print("checkrom: %s: no 3-cycle opcodes, RF read only with LDN" % sys.argv[1])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
