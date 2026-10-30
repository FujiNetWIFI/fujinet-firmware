#!/usr/bin/env bash
# apply.sh -- graft the FujiNet cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device and the firmware's own protocol sources into
# src/devices/bus/coleco/cartridge/, so MAME runs the code the cart ships, and edits bus.lua
# and exp.cpp. Idempotent.
# Then: make -C /path/to/mame -j$(nproc) REGENIE=1 (REGENIE=1 whenever bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/coleco/cartridge"
# The slot options live in the bus device itself on this platform, at the bottom of exp.cpp.
DRIVER="$BUSDIR/exp.cpp"

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_cfg fuji_mailbox_proto fuji_mailbox_check
graft "$BUSDIR" ../firmware/src/colmap.c ../firmware/include/{colmap,fuji_mailbox}.h
graft_fujins "$BUSDIR" coleco
graft_lua coleco/cartridge xin1.cpp

# Plain option_add, unlike every stock ColecoVision mapper, so -cartslot fujinet works from
# the CLI.
if ! grep -q "COLECOVISION_FUJINET" "$DRIVER"; then
    python3 - "$DRIVER" <<'PYEOF'
import sys

path = sys.argv[1]
text = open(path).read()
inc_anchor = '#include "xin1.h"\n'
opt_anchor = '\tdevice.option_add_internal("xin1", COLECOVISION_XIN1);\n'
for anchor in (inc_anchor, opt_anchor):
    if anchor not in text:
        sys.exit(f"apply.sh: exp.cpp anchor not found: {anchor!r}")
text = text.replace(inc_anchor, inc_anchor + '#include "fujinet.h"\n', 1)
text = text.replace(opt_anchor,
                    opt_anchor + '\tdevice.option_add("fujinet", COLECOVISION_FUJINET);\n',
                    1)
open(path, "w").write(text)
PYEOF
    echo "apply.sh: exp.cpp updated"
fi

echo "apply.sh: done"
