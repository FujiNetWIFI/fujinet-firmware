#!/usr/bin/env python3
"""checkdefs.py -- check a client's hand-mirrored equates against fuji_mailbox.h.

A drifted address assembles and runs, and talks to a page that decodes nothing.
PAIRS is what every client mirrors; a client using more of the window names its
extras: --extra FB_CARD=FN_BLIT_CARD,FH_BGO=FN_HOT_BLIT_GO

Usage: checkdefs.py firmware/include/fuji_mailbox.h testrom/fujinet.inc
                    [--extra ASMNAME=FN_NAME,...]
"""

import os
import re
import sys

# asm name -> C name: the addresses, register numbers and magics a client uses.
PAIRS = {
    "FNTEXT":  "FN_T_BASE",
    "FNRPLY":  "FN_R_DATA",
    "FNACKS":  "FN_R_ACKSEQ",
    "FNSTAT":  "FN_R_STATUS",
    "FNERR":   "FN_R_ERR",
    "FNRCMD":  "FN_R_REPLY_CMD",
    "FNRXLO":  "FN_R_RXLEN_LO",
    "FNRXHI":  "FN_R_RXLEN_HI",
    "FNBST":   "FN_R_BOOT_STATE",
    "FNBPC":   "FN_R_BOOT_PCT",
    "FNBER":   "FN_R_BOOT_ERR",
    "FNMAG0":  "FN_R_MAGIC0",
    "FNMAG1":  "FN_R_MAGIC1",
    "FNPVER":  "FN_R_PROTO_VER",
    "FNSECH":  "FN_R_SLICE_ECHO",
    "FNBTXG":  "FN_B_TEXTGEN",
    "FNBBNK":  "FN_B_BANK",
    "FNBFLG":  "FN_B_FLAGS",
    "FNRSEL":  "FN_H_REGSEL",
    "FNTX":    "FN_TX_BASE",
    "FR_DEV":  "FN_REG_DEVICE",
    "FR_CMD":  "FN_REG_CMD",
    "FR_NPAR": "FN_REG_NPARAM",
    "FR_DRST": "FN_REG_DATA_RST",
    "FR_RXSL": "FN_REG_RXSLICE",
    "FR_SEQ":  "FN_REG_SEQ",
    "FR_BLCK": "FN_REG_BOOTLOCK",
    "FH_BANK": "FN_HOT_BANK",
    "FH_TROW": "FN_HOT_TROW",
    "FH_TCHR": "FN_HOT_TCHR",
    "FH_TEND": "FN_HOT_TEND",
    "FNPLNL":  "FN_B_PATHLEN",
    "FP_TXRAW": "FN_PATH_TXRAW",
    "FNTAIL":  "FN_FIXED_TAIL",
    "FH_PATHC": "FN_HOT_PATH_CH",
    "FH_PATHO": "FN_HOT_PATH_OP",
    "FP_RST":  "FN_PATH_RST",
    "FP_POP":  "FN_PATH_POP",
    "FP_TX":   "FN_PATH_TX",
    "FH_ARM1": "FN_HOT_ARM1",
    "FH_ARM2": "FN_HOT_ARM2",
    "FH_SWAP": "FN_HOT_SWAP",
    "FNAM1":   "FN_ARM_MAGIC1",
    "FNAM2":   "FN_ARM_MAGIC2",
    "FNBLKM":  "FN_BOOTLOCK_MAGIC",
    "FNTCOL":  "FN_T_COLS",
}

# Values the .inc composes rather than mirroring one-for-one.
DERIVED = {
    "FNCMT": lambda c: c["FN_H_REGSEL"] + c["FN_HOT_COMMIT"],
}


# pico/common/include, for the shared defines fuji_mailbox.h includes. From this script,
# not the cwd: the game ports run it from their own trees.
COMMON_INC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "..", "common", "include")


def read_c(path):
    out = {}
    text = open(path).read()
    for inc in re.findall(r'^#include\s+"(\w+\.h)"', text, re.M):
        for d in (os.path.dirname(path), COMMON_INC):
            if os.path.exists(os.path.join(d, inc)):
                out.update(read_c(os.path.join(d, inc)))
                break
    for name, val in re.findall(
            r"^#define\s+(FN_\w+)\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:/\*|$)",
            text, re.M):
        out[name] = int(val, 0)
    return out


def read_asm(path):
    out = {}
    for name, val in re.findall(r"^(\w+)\s+EQU\s+(\$[0-9A-Fa-f]+|\d+)",
                                open(path).read(), re.M):
        out[name] = int(val[1:], 16) if val.startswith("$") else int(val)
    return out


def read_extra(argv):
    """--extra ASMNAME=FN_NAME,... -- equates this client uses and the
    testroms do not."""
    out = {}
    if "--extra" in argv:
        for pair in argv[argv.index("--extra") + 1].split(","):
            if not pair.strip():
                continue
            asm_name, _, c_name = pair.partition("=")
            if not c_name:
                sys.exit("checkdefs: --extra wants ASMNAME=FN_NAME, got %r"
                         % pair)
            out[asm_name.strip()] = c_name.strip()
    return out


def main():
    c = read_c(sys.argv[1])
    a = read_asm(sys.argv[2])
    pairs = dict(PAIRS)
    pairs.update(read_extra(sys.argv))
    bad = []

    for asm_name, c_name in pairs.items():
        if c_name not in c:
            bad.append("%s is not defined in the header" % c_name)
        elif asm_name not in a:
            bad.append("%s is not defined in the .inc" % asm_name)
        elif a[asm_name] != c[c_name]:
            bad.append("%s = $%04X but %s = $%04X"
                       % (asm_name, a[asm_name], c_name, c[c_name]))

    for asm_name, fn in DERIVED.items():
        try:
            want = fn(c)
        except KeyError as e:
            bad.append("cannot derive %s: %s missing from the header" % (asm_name, e))
            continue
        if asm_name not in a:
            bad.append("%s is not defined in the .inc" % asm_name)
        elif a[asm_name] != want:
            bad.append("%s = $%04X but the header implies $%04X"
                       % (asm_name, a[asm_name], want))

    for p in bad:
        print("checkdefs: %s" % p, file=sys.stderr)
    if bad:
        print("checkdefs: the client's equates have drifted from the spec",
              file=sys.stderr)
        return 1
    print("checkdefs: %d equates agree with fuji_mailbox.h"
          % (len(pairs) + len(DERIVED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
