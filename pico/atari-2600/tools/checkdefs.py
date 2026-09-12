#!/usr/bin/env python3
"""checkdefs.py -- keep testrom/fujinet.inc honest against fuji_mailbox.h.

The console client's equates are hand-mirrored from the firmware header, which
is the family's convention and is worth keeping: the .inc is readable 6502
source with its own comments, not a generated blob. But hand-mirroring drifts,
and this is what drift looks like on this port:

  the spec put the control page at $1D00, the .inc said $1C00, and every
  register write the client made went to a page that decodes nothing. It
  assembled, it ran, the display came up -- and the mailbox was simply never
  armed. Reads of $1C00 fell through to the served window and returned 0, so
  even the symptom pointed somewhere else.

Nothing catches that except comparing the two files, so the build does.

Usage: checkdefs.py firmware/include/fuji_mailbox.h testrom/fujinet.inc
"""

import re
import sys

# asm name -> C name. Only the values a client can get WRONG are listed:
# addresses it stores to or reads from, register numbers, and magic values.
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


def read_c(path):
    out = {}
    for name, val in re.findall(
            r"^#define\s+(FN_\w+)\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:/\*|$)",
            open(path).read(), re.M):
        out[name] = int(val, 0)
    return out


def read_asm(path):
    out = {}
    for name, val in re.findall(r"^(\w+)\s+EQU\s+(\$[0-9A-Fa-f]+|\d+)",
                                open(path).read(), re.M):
        out[name] = int(val[1:], 16) if val.startswith("$") else int(val)
    return out


def main():
    c = read_c(sys.argv[1])
    a = read_asm(sys.argv[2])
    bad = []

    for asm_name, c_name in PAIRS.items():
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
    print("checkdefs: %d equates agree with fuji_mailbox.h" % (len(PAIRS) + len(DERIVED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
