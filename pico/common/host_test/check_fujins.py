#!/usr/bin/env python3
"""check_fujins.py -- emu/fujins.syms must list exactly what each module exports.

    check_fujins.py OUTDIR MAILBOX_INCLUDE_DIR

A function missing from the list keeps one name in every graft, so the carts sharing a
MAME binary silently share one copy, built against the wrong fuji_mailbox.h.
"""
import os
import subprocess
import sys

COMMON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def main():
    out, mailbox = sys.argv[1], sys.argv[2]
    bad = 0
    for line in open(os.path.join(COMMON, "emu", "fujins.syms")):
        if line.startswith("#") or not line.strip():
            continue
        module, *listed = line.split()
        src = next(p for p in (os.path.join(COMMON, d, module + ".c") for d in ("src", "emu"))
                   if os.path.exists(p))
        obj = os.path.join(out, module + ".o")
        subprocess.run(["cc", "-c", "-std=gnu11", "-w", "-I" + os.path.join(COMMON, "include"),
                        "-I" + os.path.join(COMMON, "emu"), "-I" + mailbox, "-o", obj, src],
                       check=True)
        nm = subprocess.run(["nm", "--defined-only", "-g", obj], check=True,
                            capture_output=True, text=True).stdout.split("\n")
        exported = {f[2] for f in (l.split() for l in nm) if len(f) == 3}
        for name in sorted(exported - set(listed)):
            print("fujins.syms: %s exports %s, which is not listed" % (module, name))
            bad = 1
        for name in sorted(set(listed) - exported):
            print("fujins.syms: %s lists %s, which it does not export" % (module, name))
            bad = 1
    print("check_fujins: %s" % ("FAILED" if bad else "every module matches"))
    sys.exit(bad)


if __name__ == "__main__":
    main()
