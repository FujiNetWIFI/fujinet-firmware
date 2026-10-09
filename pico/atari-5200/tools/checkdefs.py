#!/usr/bin/env python3
"""checkdefs.py -- the hand-made mirrors must agree with fuji_mailbox.h.

Every FN_R_* status offset, FN_REG_* number, magic and arena address that
testrom/fujinet.inc mirrors is compared against the C header it was copied
from; with --lib, so is fujinet-lib's bus/atari5200/fujinet-bus-atari5200.h.

Usage: checkdefs.py [--lib <fujinet-lib checkout>]   (from pico/atari-5200)
"""

import os
import re
import sys

HDR = "firmware/include/fuji_mailbox.h"
INC = "testrom/fujinet.inc"


def c_defs(path):
    defs = {}
    for line in open(path):
        m = re.match(r"#define\s+(FN_\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b", line)
        if m:
            defs[m.group(1)] = int(m.group(2), 0)
    return defs


def inc_defs(path):
    defs = {}
    for line in open(path):
        m = re.match(r"(FN\w*)\s*=\s*([^;]+)", line)
        if not m:
            continue
        expr = m.group(2).strip().replace("$", "0x")
        try:
            defs[m.group(1)] = eval(expr, {}, dict(defs))
        except Exception:
            pass
    return defs


def lib_defs(path):
    """The lib header's #define NAME (... 0xADDR) and #define NAME 0xNN."""
    defs = {}
    for line in open(path):
        m = re.match(r"#define\s+(FN\w+)\s+\(?\*?\(?(?:volatile uint8_t \*\)\s*)?(0x[0-9A-Fa-f]+|\d+)", line)
        if m:
            defs[m.group(1)] = int(m.group(2), 0)
    return defs


def check_lib(c, root):
    path = os.path.join(root, "bus", "atari5200", "fujinet-bus-atari5200.h")
    lib = lib_defs(path)
    base = c["FN_ARENA_BASE"]
    pairs = [("FN_REPLY", base + c["FN_R_DATA"]), ("FN_REPLY_MAX", c["FN_R_SLICE_LEN"]),
             ("FN_ACKSEQ", base + c["FN_R_ACKSEQ"]), ("FN_STATUS", base + c["FN_R_STATUS"]),
             ("FN_ERRCODE", base + c["FN_R_ERR"]), ("FN_REPLYCMD", base + c["FN_R_REPLY_CMD"]),
             ("FN_RXLEN_LO", base + c["FN_R_RXLEN_LO"]), ("FN_RXLEN_HI", base + c["FN_R_RXLEN_HI"]),
             ("FN_BOOTSTAT", base + c["FN_R_BOOT_STATE"]), ("FN_BOOTPCT", base + c["FN_R_BOOT_PCT"]),
             ("FN_BOOTERR", base + c["FN_R_BOOT_ERR"]), ("FN_MAGIC0", base + c["FN_R_MAGIC0"]),
             ("FN_MAGIC1", base + c["FN_R_MAGIC1"]), ("FN_PROTOVER", base + c["FN_R_PROTO_VER"]),
             ("FN_MODE", base + c["FN_R_MODE"]), ("FN_MAPPER", base + c["FN_R_MAPPER"]),
             ("FN_LINK", base + c["FN_R_LINK"]), ("FN_BOOTGOT", base + c["FN_R_BOOT_GOT0"]),
             ("FN_BOOTTOT", base + c["FN_R_BOOT_TOT0"]), ("FN_ARMED", base + c["FN_R_ARMED"]),
             ("FN_STAGED", base + c["FN_R_STAGED"]), ("FN_STAGEDKIND", base + c["FN_R_STAGED_KIND"]),
             ("FN_STAGEDCRC", base + c["FN_R_STAGED_CRC"]), ("FN_SWAPS", base + c["FN_R_SWAPS"]),
             ("FN_PROTO_VER", c["FN_PROTO_VER"]), ("FN_TX_MAX", c["FN_TX_MAX"]),
             ("FN_BOOTLOCK_MAGIC", c["FN_BOOTLOCK_MAGIC"]), ("FN_CONFIG_MAGIC", c["FN_CONFIG_MAGIC"])]
    regs = {"FNR_DEVICE": "FN_REG_DEVICE", "FNR_CMD": "FN_REG_CMD", "FNR_NPARAM": "FN_REG_NPARAM",
            "FNR_DATA_RST": "FN_REG_DATA_RST", "FNR_RXSLICE": "FN_REG_RXSLICE",
            "FNR_SEQ": "FN_REG_SEQ", "FNR_BOOTLOCK": "FN_REG_BOOTLOCK", "FNR_CONFIG": "FN_REG_CONFIG"}
    pairs += [(k, c[v]) for k, v in regs.items()]
    bad = [(n, lib.get(n), v) for n, v in pairs if lib.get(n) != v]
    for n, got, want in bad:
        print("checkdefs: %s is %s in %s, %#x in %s" % (n, got if got is None else hex(got),
                                                      path, want, HDR), file=sys.stderr)
    if not bad:
        print("checkdefs: %s agrees with %s (%d names)" % (path, HDR, len(pairs)))
    return 1 if bad else 0


def main():
    c, a = c_defs(HDR), inc_defs(INC)
    base = c["FN_ARENA_BASE"]
    pairs = [
        ("FN_REPLY", base + c["FN_R_DATA"]),
        ("FN_STATUS", base + c["FN_R_BASE"]),
        ("FN_REGSEL", base + c["FN_H_REGSEL"]),
        ("FN_REGDATA", base + c["FN_H_REGDATA"]),
        ("FN_TXDATA", base + c["FN_H_DATA"]),
        ("FN_STUB", base + c["FN_H_REGSEL"] + c["FN_HOT_STUB"]),
        ("FN_CLAIM", c["FN_CLAIM_ADDR"]),
    ]
    status = {"FN_ACKSEQ": "FN_R_ACKSEQ", "FN_STAT": "FN_R_STATUS", "FN_ERR": "FN_R_ERR",
              "FN_REPLYCMD": "FN_R_REPLY_CMD", "FN_RXLEN": "FN_R_RXLEN_LO",
              "FN_BOOTSTATE": "FN_R_BOOT_STATE", "FN_BOOTPCT": "FN_R_BOOT_PCT",
              "FN_BOOTERR": "FN_R_BOOT_ERR", "FN_MAGIC": "FN_R_MAGIC0",
              "FN_PROTOVER": "FN_R_PROTO_VER", "FN_MODE": "FN_R_MODE",
              "FN_MAPPER": "FN_R_MAPPER", "FN_LINK": "FN_R_LINK",
              "FN_BOOTGOT": "FN_R_BOOT_GOT0", "FN_BOOTTOT": "FN_R_BOOT_TOT0",
              "FN_ARMED": "FN_R_ARMED", "FN_STAGED": "FN_R_STAGED",
              "FN_STAGEDKIND": "FN_R_STAGED_KIND", "FN_STAGEDCRC": "FN_R_STAGED_CRC",
              "FN_SWAPS": "FN_R_SWAPS"}
    for inc, hdr in status.items():
        pairs.append((inc, base + c[hdr]))
    for name in ("FN_REG_DEVICE", "FN_REG_CMD", "FN_REG_NPARAM", "FN_REG_DATA_RST",
                 "FN_REG_RXSLICE", "FN_REG_SEQ", "FN_REG_BOOTLOCK", "FN_REG_BOOTSEL_1",
                 "FN_REG_BOOTSEL_2", "FN_REG_CONFIG", "FN_BOOTLOCK_MAGIC",
                 "FN_CONFIG_MAGIC", "FN_BOOT_READY", "FN_BOOT_FAILED",
                 "FN_MODE_BOOT", "FN_MODE_GAME", "FN_MODE_APP"):
        pairs.append((name, c[name]))
    bad = [(n, a.get(n), v) for n, v in pairs if a.get(n) != v]
    for n, got, want in bad:
        print("checkdefs: %s is %s in %s, %#x in %s" % (n, got if got is None else hex(got),
                                                      INC, want, HDR), file=sys.stderr)
    if bad:
        return 1
    print("checkdefs: %s agrees with %s (%d names)" % (INC, HDR, len(pairs)))
    if len(sys.argv) == 3 and sys.argv[1] == "--lib":
        return check_lib(c, sys.argv[2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
