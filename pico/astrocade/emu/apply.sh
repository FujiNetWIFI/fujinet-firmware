#!/usr/bin/env bash
# apply.sh -- graft the FujiNet cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model (fujinet.cpp/h, fujitcp.c/h) and the cartridge
# firmware's own protocol sources (fujimail, fujibus, astromap, and their
# headers -- MAME then runs the code the cartridge actually ships) into
# src/devices/bus/astrocde/, and makes the only two shared-file edits:
# the bus.lua file list and the cart-slot option in astrohome.cpp.
# Idempotent: run it again after editing any source here.
#
# Afterwards: make -C /path/to/mame -j$(nproc) REGENIE=1 SUBTARGET=... as
# usual (REGENIE=1 is required the first time because bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/astrocde"
BUSLUA="$MAME/scripts/src/bus.lua"
DRIVER="$MAME/src/mame/midway/astrohome.cpp"

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }

# The shared protocol sources are C, but MAME's device project forces a C++
# precompiled header on every file it owns, so they are copied in under .cpp
# names and compiled as C++ (they are written to build both ways).
cp fujinet.cpp fujinet.h fujitcp.h "$BUSDIR/"
cp fujitcp.c "$BUSDIR/fujitcp.cpp"
cp ../firmware/src/fujimail.c "$BUSDIR/fujimail.cpp"
cp ../firmware/src/fujibus.c "$BUSDIR/fujibus.cpp"
cp ../firmware/src/astromap.c "$BUSDIR/astromap.cpp"
cp ../firmware/include/fujimail.h ../firmware/include/fujibus.h \
   ../firmware/include/astromap.h ../firmware/include/fuji_mailbox.h \
   "$BUSDIR/"
rm -f "$BUSDIR/fujitcp.c" "$BUSDIR/fujimail.c" "$BUSDIR/fujibus.c" \
      "$BUSDIR/astromap.c"

# bus.lua: add our files to the ASTROCADE block, once.
if ! grep -q "astrocde/fujinet.cpp" "$BUSLUA"; then
    python3 - "$BUSLUA" <<'EOF'
import sys

path = sys.argv[1]
text = open(path).read()
anchor = '\t\tMAME_DIR .. "src/devices/bus/astrocde/lightpen.h",\n'
if anchor not in text:
    sys.exit("apply.sh: bus.lua anchor line not found")
addition = anchor + "".join(
    f'\t\tMAME_DIR .. "src/devices/bus/astrocde/{f}",\n'
    for f in ("fujinet.cpp", "fujinet.h", "fujitcp.cpp", "fujitcp.h",
              "fujimail.cpp", "fujimail.h", "fujibus.cpp", "fujibus.h",
              "astromap.cpp", "astromap.h", "fuji_mailbox.h"))
open(path, "w").write(text.replace(anchor, addition, 1))
EOF
    echo "apply.sh: bus.lua updated (build with REGENIE=1)"
fi

# astrohome.cpp: include + selectable slot option, once. Plain option_add,
# unlike the internal ROM types, so -cartslot fujinet works from the CLI.
if ! grep -q "ASTROCADE_ROM_FUJINET" "$DRIVER"; then
    python3 - "$DRIVER" <<'EOF'
import sys

path = sys.argv[1]
text = open(path).read()
inc_anchor = '#include "bus/astrocde/rom.h"\n'
opt_anchor = '\tdevice.option_add_internal("rom_cass",  ASTROCADE_ROM_CASS);\n'
for a in (inc_anchor, opt_anchor):
    if a not in text:
        sys.exit(f"apply.sh: astrohome.cpp anchor not found: {a!r}")
text = text.replace(inc_anchor,
                    inc_anchor + '#include "bus/astrocde/fujinet.h"\n', 1)
text = text.replace(opt_anchor,
                    opt_anchor
                    + '\tdevice.option_add("fujinet",           ASTROCADE_ROM_FUJINET);\n',
                    1)
open(path, "w").write(text)
EOF
    echo "apply.sh: astrohome.cpp updated"
fi

echo "apply.sh: done"
