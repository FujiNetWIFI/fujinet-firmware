#!/usr/bin/env bash
# apply.sh -- graft the FujiNet Atari 7800 cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device and the cart firmware's sources into src/devices/bus/a7800/,
# adds them to scripts/src/bus.lua and adds a "fujinet" option to a78_carts.h.
# The 7800 driver is not touched. Idempotent; rerun after editing a source.
#
# Afterwards: make -C /path/to/mame -j$(nproc) REGENIE=1 NOWERROR=1
# (REGENIE=1 whenever apply.sh says bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/a7800"
CARTS="$BUSDIR/a78_carts.h"
FW=../firmware
GEN=../build/gen
[ -f "$GEN/fujiconfigrom.h" ] || GEN=$FW/baked      # no fresh build: the baked set

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }
for f in fujiconfigrom.h a78loaderrom.h a78bootblk.h; do
    [ -f "$GEN/$f" ] || { echo "apply.sh: $GEN/$f missing; run ../build-cart.sh" >&2; exit 1; }
done

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_cfg fuji_crc fuji_mailbox_proto \
    fuji_mailbox_check
graft "$BUSDIR" "$FW"/src/{a78map,a78map_db,fuji_load,hsc}.c \
    "$FW"/include/{fuji_mailbox,a78_cart,a78map,a78map_db,fuji_load,hsc}.h \
    "$GEN"/{fujiconfigrom,a78loaderrom,a78bootblk}.h
graft_fujins "$BUSDIR" a78 fuji_load.cpp fuji_load.h hsc.cpp hsc.h -- \
    fuji_load_init fuji_load_stage fuji_load_unstage fuji_load_arm fuji_load_busy \
    fuji_load_event fuji_load_ack fuji_load_abort fuji_load_handover fuji_load_slices \
    fuji_load_slice hsc_init hsc_restore hsc_service hsc_save_chunk
graft_lua a7800 rom.h

if ! grep -q "A78_FUJINET" "$CARTS"; then
    python3 - "$CARTS" <<'EOF2'
import sys
path = sys.argv[1]
text = open(path).read()
inc = '#include "cpuwiz.h"\n'
if inc not in text:
    sys.exit("apply.sh: a78_carts.h include anchor not found")
text = text.replace(inc, inc + '#include "fujinet.h"\n', 1)
anchor = 'static void a7800_cart(device_slot_interface &device)\n{\n'
if anchor not in text:
    sys.exit("apply.sh: a78_carts.h option list not found")
opt = ('\t// FujiNet cartridge (pico/atari-7800 in fujinet-firmware): selectable\n'
       '\tdevice.option_add("fujinet",      A78_FUJINET);\n')
text = text.replace(anchor, anchor + opt, 1)
open(path, "w").write(text)
EOF2
    echo "apply.sh: a78_carts.h updated"
fi

echo "apply.sh: done"
