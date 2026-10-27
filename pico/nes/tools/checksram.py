#!/usr/bin/env python3
"""checksram.py -- fail the build if core1's code is not in SRAM (a missed
bus cycle otherwise).

Usage: checksram.py fujines.elf
"""

import re
import subprocess
import sys

SRAM = 0x20000000
REQUIRED = ("nes_core1_main", "nesmap_write", "fuji_cart_note", "nes_pio_patch_prg",
            "nes_pio_patch_chr")


def main():
    elf = sys.argv[1]
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True,
                         text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    bad = []
    for name in REQUIRED:
        if name not in syms:
            if name.startswith(("fuji_cart_note", "nes_pio_patch")):
                continue                     # inlined away: fine
            bad.append("%s is not in the image at all" % name)
        elif syms[name] < SRAM:
            bad.append("%s is at %#010x -- in FLASH, not SRAM" % (name, syms[name]))
    for p in bad:
        print("checksram: %s" % p, file=sys.stderr)
    if bad:
        return 1
    print("checksram: core1 is SRAM-resident (%s)"
          % ", ".join("%s@%#x" % (n, syms[n]) for n in REQUIRED if n in syms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
