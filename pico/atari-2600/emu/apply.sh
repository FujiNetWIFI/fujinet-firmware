#!/usr/bin/env bash
# apply.sh [mame-tree] -- graft the FujiNet cartridge device into MAME (default ~/Workspace/mame).
#
# Copies the device, pico/common's mailbox service and the cart's own decode and
# renderer into src/devices/bus/vcs/, and edits bus.lua and a2600.cpp; exits
# nonzero if an anchor moved. Re-run after any edit to a shared source, then
# make -C <mame> -j$(nproc) NOWERROR=1 REGENIE=1.

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:-$HOME/Workspace/mame}
BUSDIR="$MAME/src/devices/bus/vcs"
DRIVER="$MAME/src/mame/atari/a2600.cpp"
FW=../firmware

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }
[ -f "$DRIVER" ] || { echo "apply.sh: $DRIVER missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

rm -f "$BUSDIR"/{fujitcp,fujimail,fujibus,fuji_cfg,vcs_render}.c
graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_cfg fuji_mailbox_proto
graft "$BUSDIR" "$FW"/src/vcs_render.c \
    "$FW"/include/{fuji_mailbox,vcs_cart,vcs_render,vcsmap}.h
python3 ../tools/mkfont.py > "$BUSDIR/vcs_font.h"
GRAFTED+=(vcs_font.h)
graft_fujins "$BUSDIR" a2600 vcs_render.cpp vcs_render.h -- \
    vcs_render_row vcs_render_clear vcs_render_cell vcs_render_path_row \
    vcs_render_path_poke vcs_render_path_tile vcs_render_cards vcs_blit
graft_lua vcs rom.h

# a2600.cpp: the include and the slot option.
if ! grep -q 'A26_ROM_FUJINET' "$DRIVER"; then
    python3 - "$DRIVER" <<'EOF'
import sys

path = sys.argv[1]
text = open(path).read()

inc_anchor = '#include "bus/vcs/rom.h"\n'
if inc_anchor not in text:
    sys.exit("apply.sh: a2600.cpp include anchor not found")
text = text.replace(inc_anchor, '#include "bus/vcs/fujinet.h"\n' + inc_anchor, 1)

# Never auto-detected: identify_cart_type() would size-match a client to a
# stock ROM type, so it takes -cartslot fujinet.
opt_anchor = '\tdevice.option_add("a26_2k_4k", A26_ROM_2K_4K);\n'
if opt_anchor not in text:
    sys.exit("apply.sh: a2600.cpp slot option anchor not found")
text = text.replace(opt_anchor,
                    opt_anchor + '\tdevice.option_add("fujinet",   A26_ROM_FUJINET);\n', 1)

open(path, "w").write(text)
EOF
    echo "apply.sh: a2600.cpp updated"
fi

echo "apply.sh: grafted into $MAME"
echo "  build:  make -C $MAME -j\$(nproc) NOWERROR=1 REGENIE=1"
echo "  run:    ./run.sh fujitest  (SLOT=fujinet)"
