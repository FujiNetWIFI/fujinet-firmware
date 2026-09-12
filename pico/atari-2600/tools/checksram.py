#!/usr/bin/env python3
"""checksram.py -- core1 and everything it calls must live in SRAM.

The serve path has roughly 500 ns from the address settling to the data being
required at the connector. A single XIP cache miss is most of that, and a
missed bus cycle on this console does not slow anything down -- it serves the
wrong byte, and the 6507 executes it. The ColecoVision port dropped its flash
tier over the same arithmetic with 373 ns to spend.

So this is a build-time assertion, not a guideline: the core1 entry point and
every symbol it reaches must be at 0x2000xxxx. It is easy to break by
accident -- adding one helper without __not_in_flash_func, or letting a
memcpy() creep into the bus loop, is enough.

Usage: checksram.py fujivcs.elf
"""

import re
import subprocess
import sys

SRAM = 0x20000000

# Must be in SRAM: core1's entry and everything the bus loop can reach.
REQUIRED = ("vcs_core1_main", "fuji_cart_serve_staged", "fuji_cart_poke")


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
            bad.append("%s is not in the image at all" % name)
        elif syms[name] < SRAM:
            bad.append("%s is at %#010x -- in FLASH, not SRAM" % (name, syms[name]))

    for p in bad:
        print("checksram: %s" % p, file=sys.stderr)
    if bad:
        print("checksram: the bus loop would fault to flash and miss cycles",
              file=sys.stderr)
        return 1

    print("checksram: core1 is SRAM-resident (%s)"
          % ", ".join("%s@%#x" % (n, syms[n]) for n in REQUIRED))
    return 0


if __name__ == "__main__":
    sys.exit(main())
