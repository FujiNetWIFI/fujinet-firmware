#!/usr/bin/env bash
# apply-mame.sh -- graft the FujiNet Odyssey 2 cartridge into a MAME tree.
#
# Usage: ./apply-mame.sh /path/to/mame
#
# Copies the device model, pico/common's TCP transport and wire codec, and the
# cartridge's own protocol sources into src/devices/bus/odyssey2/, then edits:
#
#   scripts/src/bus.lua               the O2 file list
#   src/devices/bus/odyssey2/slot.cpp the "fujinet" slot option
#
# Idempotent: run it again after editing any source here. Afterwards:
# make -C /path/to/mame -j$(nproc) REGENIE=1 (REGENIE=1 whenever this says
# bus.lua changed), then
#
#   mame odyssey2 -cartslot fujinet -cart image.bin
#
# FUJINET_TCP=host:port picks the fujinet-pc (default 127.0.0.1:9995) and
# FUJINET_DEBUG=1 logs every mailbox transaction.

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply-mame.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/odyssey2"
SLOT_C="$BUSDIR/slot.cpp"

[ -d "$BUSDIR" ] || { echo "apply-mame.sh: $BUSDIR missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujibus
graft "$BUSDIR" ../firmware/src/{fujimail,o2map}.c \
                ../firmware/include/{fujimail,o2map,o2app,fuji_mailbox}.h
# Namespace the O2's own fujimail/o2map so other carts' copies in MAME don't clash.
graft_fujins "$BUSDIR" o2 fujinet.cpp fujimail.cpp fujimail.h o2map.cpp o2map.h -- \
    fujimail_init fujimail_paint fujimail_write fujimail_inbound \
    fujimail_rx fujimail_rxn fujimail_stage_live \
    o2map_plan o2map_is_appbank o2map_claims_mailbox o2map_apply \
    o2map_file_chunk o2map_strerror
graft_lua odyssey2 slot.h

# slot.cpp: include, and a selectable slot option.
if ! grep -q "O2_ROM_FUJINET" "$SLOT_C"; then
    python3 - "$SLOT_C" <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()

inc = '#include "slot.h"\n'
if inc not in text:
    sys.exit("apply-mame.sh: slot.cpp include anchor not found")
text = text.replace(inc, inc + '#include "fujinet.h"\n', 1)

opt = '\tdevice.option_add_internal("o2_rom",      O2_ROM_STD);\n'
if opt not in text:
    sys.exit("apply-mame.sh: slot.cpp option anchor not found")
text = text.replace(opt, opt + '\tdevice.option_add("fujinet",              O2_ROM_FUJINET);\n', 1)
open(path, "w").write(text)
EOF
    echo "apply-mame.sh: slot.cpp updated"
fi

echo "apply-mame.sh: done"
