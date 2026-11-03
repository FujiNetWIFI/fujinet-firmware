#!/usr/bin/env bash
# apply.sh -- graft the FujiNet cartridge device into a MAME tree.
#
# Usage: ./apply.sh [/path/to/mame]      default: ~/Workspace/mame
#
# Copies the device model (fujinet.cpp/h), pico/common's mailbox service and the
# cartridge firmware's own sources (the bus decode, the glyph compositor and
# their headers -- so MAME runs the code the cartridge actually ships, not a
# lookalike) into src/devices/bus/vcs/, and makes two shared-file edits:
#
#   scripts/src/bus.lua              the VCS file list
#   src/mame/atari/a2600.cpp         the slot option and its include
#
# Idempotent, and every edit is anchored: if MAME moves an anchor this exits
# nonzero rather than half-patching.
#
# WHAT IS DELIBERATELY NOT PATCHED. vcs_cart_slot_device::call_load() rejects
# any image whose size is not on a hard whitelist (0x800, 0x1000, 0x2000,
# 0x28ff, 0x2900, 0x3000, 0x4000, 0x8000, 0x10000, 0x80000). A FujiNet client
# is (N+1) * 2048 bytes -- N banks plus the fixed half -- and N in {1,3,7,15}
# gives exactly 4K, 8K, 16K and 32K, all whitelisted. So the constraint costs
# nothing and the whitelist stays untouched: every core file this script edits
# is one more thing that breaks silently on a MAME upgrade, and tools/checkrom.py
# enforces the size on our side where it belongs.
#
# Afterwards: make -C /path/to/mame -j$(nproc) NOWERROR=1 REGENIE=1
# (REGENIE=1 is required the first time, because bus.lua changed. Re-run THIS
# script after every edit to a shared source -- MAME builds the copies.)

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

# The slot option list. Ours is added explicitly and is never auto-detected:
# identify_cart_type() would size-match a FujiNet client to a stock ROM type,
# so the client is always selected with -cartslot fujinet.
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
