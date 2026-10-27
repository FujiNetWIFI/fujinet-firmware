#!/usr/bin/env bash
# apply.sh -- graft the FujiNet NES cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model and the firmware's own protocol sources and baked
# ROMs (so MAME runs what the cart ships) into src/devices/bus/nes/, and edits:
#
#   scripts/src/bus.lua             the NES file list
#   src/devices/bus/nes/nes_carts.cpp  the "fujinet" slot option
#   src/mame/nintendo/nes.cpp       install read_h for our device type
#
# Idempotent: run it again after editing any source here. Needs
# firmware/include/fujiconfigrom.h and nesloaderrom.h: run ../build-cart.sh.
#
# Afterwards: make -C /path/to/mame -j$(nproc) REGENIE=1 NOWERROR=1
# (REGENIE=1 whenever apply.sh says bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/nes"
CARTS="$BUSDIR/nes_carts.cpp"
DRIVER="$MAME/src/mame/nintendo/nes.cpp"

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }
for f in ../firmware/include/fujiconfigrom.h ../firmware/include/nesloaderrom.h; do
    [ -f "$f" ] || { echo "apply.sh: $f missing; run ../build-cart.sh first" >&2; exit 1; }
done

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_mailbox_proto fuji_mailbox_check
graft "$BUSDIR" ../firmware/src/nesmap.c \
    ../firmware/include/{nesmap,fuji_mailbox,nes_cart,fujiconfigrom,nesloaderrom}.h
graft_fujins "$BUSDIR" nes
graft_lua nes nxrom.h

# nes_carts.cpp: the selectable slot option.
if ! grep -q "NES_FUJINET" "$CARTS"; then
    python3 - "$CARTS" <<'EOF2'
import sys
path = sys.argv[1]
text = open(path).read()
inc = '#include "zemina.h"\n'
if inc not in text:
    sys.exit("apply.sh: nes_carts.cpp include anchor not found")
text = text.replace(inc, inc + '#include "fujinet.h"\n', 1)
opt = '\tdevice.option_add_internal("nrom",             NES_NROM);\n'
if opt not in text:
    sys.exit("apply.sh: nes_carts.cpp option anchor not found")
text = text.replace(opt, opt + '// FujiNet cartridge (pico/nes in fujinet-firmware): selectable, any image\n\tdevice.option_add("fujinet",                 NES_FUJINET);\n', 1)
open(path, "w").write(text)
EOF2
    echo "apply.sh: nes_carts.cpp updated"
fi

# nes.cpp: install read_h for our device whatever pcb id the header gave.
if ! grep -q "NES_FUJINET" "$DRIVER"; then
    python3 - "$DRIVER" <<'EOF2'
import sys
path = sys.argv[1]
text = open(path).read()
inc = '#include "bus/nes/nes_carts.h"\n'
if inc not in text:
    sys.exit("apply.sh: nes.cpp include anchor not found")
text = text.replace(inc, inc + '#include "bus/nes/fujinet.h"\n', 1)
cond = 'if (std::find(std::begin(r_h_pcbs), std::end(r_h_pcbs), pcb_id) != std::end(r_h_pcbs))'
if cond not in text:
    sys.exit("apply.sh: nes.cpp r_h_pcbs anchor not found")
text = text.replace(cond, 'if (std::find(std::begin(r_h_pcbs), std::end(r_h_pcbs), pcb_id) != std::end(r_h_pcbs)\n\t\t\t|| m_cartslot->m_cart->device().type() == NES_FUJINET)', 1)
open(path, "w").write(text)
EOF2
    echo "apply.sh: nes.cpp updated"
fi

echo "apply.sh: done"
