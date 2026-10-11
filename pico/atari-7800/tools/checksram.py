#!/usr/bin/env python3
"""checksram.py -- core1 and everything it calls must live in SRAM.

The firmware runs copy_to_ram, so this should never fail; it catches a CMake
change that drops that before it shows up as a missed bus cycle.

Usage: checksram.py fuji7800.elf
"""

import re
import subprocess
import sys

SRAM = 0x20000000
REQUIRED = ("a78_core1_main", "fuji_live", "fuji_bus", "fuji_arena", "fuji_loader",
            "fuji_pokey_rd", "fuji_hsc_shadow", "a78_pio_patch", "a78_pio_load")


def main():
    out = subprocess.run(["arm-none-eabi-nm", sys.argv[1]], capture_output=True,
                         text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    bad = []
    for name in REQUIRED:
        if name not in syms:
            bad.append("%s is not in the image at all" % name)
        elif syms[name] < SRAM:
            bad.append("%s is at %#010x -- in FLASH, not SRAM" % (name, syms[name]))
    for p in bad:
        print("checksram: %s" % p, file=sys.stderr)
    if bad:
        return 1
    print("checksram: core1 is SRAM-resident (%s)"
          % ", ".join("%s@%#x" % (n, syms[n]) for n in REQUIRED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
