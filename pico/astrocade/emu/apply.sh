#!/usr/bin/env bash
# apply.sh -- graft the FujiNet cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model and the firmware's own protocol sources into
# src/devices/bus/astrocde/, and edits bus.lua and astrohome.cpp.
# Idempotent: rerun after editing any source here, then rebuild MAME with
# REGENIE=1 whenever apply.sh says bus.lua changed.

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/astrocde"
DRIVER="$MAME/src/mame/bally/astrohome.cpp"      # midway/ before MAME moved it
[ -f "$DRIVER" ] || DRIVER="$MAME/src/mame/midway/astrohome.cpp"

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_mailbox_proto fuji_mailbox_check
graft "$BUSDIR" ../firmware/src/astromap.c ../firmware/include/{astromap,fuji_mailbox}.h
graft_fujins "$BUSDIR" astrocade
graft_lua astrocde lightpen.h

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
