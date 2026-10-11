#!/usr/bin/env python3
"""checkdefs.py -- testrom/fujinet.inc's hand-made mirrors must agree with the
C headers they copy: fuji_mailbox.h (arena, status offsets, registers,
magics) and s2_text.h (hotspot pages, operations, symbols).

Usage: tools/checkdefs.py   (from pico/studio2)
"""
import re
import sys


def c_defs(path):
    defs = {}
    for line in open(path):
        m = re.match(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\b", line)
        if m:
            defs[m.group(1)] = int(m.group(2), 0)
    return defs


def inc_defs(path):
    defs = {}
    for line in open(path):
        m = re.match(r"(\w+)\s+EQU\s+([^;]+)", line)
        if not m:
            continue
        expr = m.group(2).strip()
        expr = re.sub(r"\b([0-9][0-9A-Fa-f]*)H\b", lambda h: "0x" + h.group(1), expr)
        try:
            defs[m.group(1)] = eval(expr, {}, dict(defs))
        except Exception:
            pass
    return defs


def main():
    c = c_defs("firmware/include/fuji_mailbox.h")
    c.update(c_defs("firmware/include/s2_text.h"))
    inc = inc_defs("testrom/fujinet.inc")
    base = c["FN_ARENA_BASE"]
    want = {
        "FN_REPLY": base + c["FN_R_DATA"],
        "FN_STATUS": base + c["FN_R_BASE"],
        "FN_REGSEL": base + c["FN_H_REGSEL"],
        "FN_REGDATA": base + c["FN_H_REGDATA"],
        "FN_TXDATA": base + c["FN_H_DATA"],
        "FN_STUB": base + c["FN_H_REGSEL"] + c["FN_HOT_STUB"],
        "FN_RASTER": c["FN_RASTER_BASE"],
        "FN_CLAIM": c["FN_CLAIM_ADDR"],
        "FN_STAT": base + c["FN_R_STATUS"],
        "FN_MAGIC": base + c["FN_R_MAGIC0"],
        "TX_CURSOR": c["S2T_H_CURSOR"], "TX_PUTC": c["S2T_H_PUTC"],
        "TX_OP": c["S2T_H_OP"], "TX_PARAM": c["S2T_H_PARAM"],
    }
    for name, val in c.items():
        if name.startswith("FN_R_") and name not in ("FN_R_DATA", "FN_R_BASE", "FN_R_SLICE_LEN",
                                                     "FN_R_NSLICES", "FN_R_PAINT_END", "FN_R_STATUS_LINK",
                                                     "FN_R_STATUS"):
            short = "FN_" + name[5:].replace("_", "")
            for cand in (short, "FN_" + name[5:]):
                if cand in inc:
                    want[cand] = base + val
        elif re.match(r"FN_(REG_|BOOT_|MODE_)\w+$|FN_\w+_MAGIC\d?$", name) and name in inc:
            want[name] = val
        elif name.startswith("S2T_OP_"):
            want["TOP_" + name[7:]] = val
        elif name.startswith("S2T_SYM_"):
            want["SYM_" + name[8:]] = val
    bad = []
    for name, val in sorted(want.items()):
        if name not in inc:
            bad.append("%s is missing from fujinet.inc" % name)
        elif inc[name] != val:
            bad.append("%s is $%X in fujinet.inc, $%X in the C headers" % (name, inc[name], val))
    for b in bad:
        print("checkdefs: %s" % b, file=sys.stderr)
    if bad:
        return 1
    print("checkdefs: fujinet.inc agrees with the C headers (%d names)" % len(want))
    return 0


if __name__ == "__main__":
    sys.exit(main())
