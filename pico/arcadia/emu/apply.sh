#!/usr/bin/env bash
# apply.sh -- graft the FujiNet cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model (fujinet.cpp/h, common's fujitcp.c/h) and the cartridge
# firmware's own protocol sources (fujimail, fujibus, arcmap, and their
# headers -- MAME then runs the code the cartridge actually ships) into
# src/devices/bus/arcadia/, and makes the only two shared-file edits:
# the bus.lua file list and the cart-slot option in arcadia.cpp.
# Idempotent: run it again after editing any source here.
#
# Afterwards: make -C /path/to/mame -j$(nproc) REGENIE=1 as usual
# (REGENIE=1 whenever apply.sh says bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/arcadia"
DRIVER="$MAME/src/mame/arcadia/arcadia.cpp"

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_mailbox_proto fuji_mailbox_check
graft "$BUSDIR" ../firmware/src/arcmap.c ../firmware/include/{arcmap,fuji_mailbox}.h
graft_fujins "$BUSDIR" arcadia
graft_lua arcadia rom.h

# arcadia.cpp: include + selectable slot option, once. Plain option_add,
# unlike the internal ROM types, so -cartslot fujinet works from the CLI.
if ! grep -q "ARCADIA_ROM_FUJINET" "$DRIVER"; then
    python3 - "$DRIVER" <<'EOF'
import sys

path = sys.argv[1]
text = open(path).read()
inc_anchor = '#include "arcadia.h"\n'
opt_anchor = '\tdevice.option_add_internal("golf",     ARCADIA_ROM_GOLF);\n'
for a in (inc_anchor, opt_anchor):
    if a not in text:
        sys.exit(f"apply.sh: arcadia.cpp anchor not found: {a!r}")
text = text.replace(inc_anchor,
                    inc_anchor + '#include "bus/arcadia/fujinet.h"\n', 1)
text = text.replace(opt_anchor,
                    opt_anchor
                    + '\tdevice.option_add("fujinet",           ARCADIA_ROM_FUJINET);\n',
                    1)
open(path, "w").write(text)
EOF
    echo "apply.sh: arcadia.cpp updated"
fi

echo "apply.sh: done"
