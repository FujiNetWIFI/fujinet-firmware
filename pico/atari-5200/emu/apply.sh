#!/usr/bin/env bash
# apply.sh -- graft the FujiNet Atari 5200 cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model (a5200_fujinet.cpp/h, common's fujitcp.c/h) and the
# cartridge firmware's own sources (fujimail, fuji_cfg, fuji_crc, fujibus, a52map,
# fuji_load, the baked CONFIG) into src/devices/bus/a800/, and edits:
#
#   scripts/src/bus.lua                    the a800 file list
#   src/devices/bus/a800/a800_carts.cpp    a selectable "fujinet" option
#
# The 5200 driver is not touched. Idempotent: run it again after editing any
# source here. CONFIG comes from build/gen (../build-cart.sh) or
# firmware/baked/.
#
# Afterwards, in a single-driver tree:
#   make SUBTARGET=a5200 SOURCES=src/mame/atari/atari400.cpp REGENIE=1 NOWERROR=1
# (REGENIE=1 whenever apply.sh says bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/a800"
CARTS="$BUSDIR/a800_carts.cpp"
FW=../firmware
GEN=../build/gen
[ -f "$GEN/fujiconfigrom.h" ] || GEN=$FW/baked      # no fresh build: the baked one

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }
[ -f "$GEN/fujiconfigrom.h" ] || { echo "apply.sh: $GEN/fujiconfigrom.h missing; run ../build-cart.sh" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" a5200_fujinet.cpp a5200_fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_cfg fuji_crc fuji_mailbox_proto \
    fuji_mailbox_check
graft "$BUSDIR" "$FW"/src/{a52map,a52map_db,fuji_load}.c \
    "$FW"/include/{fuji_mailbox,a52map,a52map_db,fuji_load}.h "$GEN/fujiconfigrom.h"
graft_fujins "$BUSDIR" a52 fuji_load.cpp fuji_load.h -- \
    fuji_load_init fuji_load_target fuji_load_commit fuji_load_stage_config fuji_load_arm
graft_lua a800 a5200_supercart.h

if ! grep -q "A5200_FUJINET" "$CARTS"; then
    python3 - "$CARTS" <<'EOF2'
import sys
path = sys.argv[1]
text = open(path).read()
inc = '#include "a800_carts.h"\n'
if inc not in text:
    sys.exit("apply.sh: a800_carts.cpp include anchor not found")
text = text.replace(inc, inc + '#include "a5200_fujinet.h"\n', 1)
anchor = 'void a5200_carts(device_slot_interface &device)\n{\n'
if anchor not in text:
    sys.exit("apply.sh: a800_carts.cpp a5200 option list not found")
opt = ('\t// FujiNet cartridge (pico/atari-5200 in fujinet-firmware): selectable\n'
       '\tdevice.option_add("fujinet",         A5200_FUJINET);\n')
text = text.replace(anchor, anchor + opt, 1)
open(path, "w").write(text)
EOF2
    echo "apply.sh: a800_carts.cpp updated"
fi

echo "apply.sh: done"
