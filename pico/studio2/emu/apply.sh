#!/usr/bin/env bash
# apply.sh -- graft the FujiNet Studio II cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model (studio2_fujinet.cpp/h, common's fujitcp.c/h) and the
# cartridge firmware's own sources (fujimail, fujibus, s2map, fuji_crc, fuji_load,
# s2_text with its font, the baked CONFIG) into src/mame/rca/, and edits
# src/mame/rca/studio2.cpp: the include, a name for the console RAM share,
# and the device in the studio2 machine. The device stays inert unless
# $FUJINET, $FUJINET_BOOT or $FUJINET_IMAGE is set, so the same binary is
# also stock MAME (Tier A's reference side).
#
# Idempotent: run it again after editing any source here. CONFIG comes from
# build/gen (../build-cart.sh) or firmware/baked/. Afterwards, in a
# single-driver tree (REGENIE=1 whenever the file list changed):
#   make SUBTARGET=studio2 SOURCES=src/mame/rca/studio2.cpp REGENIE=1 NOWERROR=1

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
DIR="$MAME/src/mame/rca"
DRV="$DIR/studio2.cpp"
FW=../firmware
GEN=../build/gen
[ -f "$GEN/fujiconfigrom.h" ] || GEN=$FW/baked      # no fresh build: the baked one

[ -f "$DRV" ] || { echo "apply.sh: $DRV missing" >&2; exit 1; }
[ -f "$GEN/fujiconfigrom.h" ] || { echo "apply.sh: $GEN/fujiconfigrom.h missing; run ../build-cart.sh" >&2; exit 1; }

# A single-driver build finds the grafted sources as the siblings of the headers
# studio2.cpp pulls in, so there is no file list to edit. The font has no header of its
# own, so it rides along in s2_text.cpp.
. ../../common/emu/graft.lib.sh

graft "$DIR" studio2_fujinet.cpp studio2_fujinet.h
graft_common "$DIR" fujitcp fujimail fujibus fuji_crc fuji_mailbox_proto fuji_mailbox_check
graft "$DIR" "$FW"/src/{s2map,fuji_load}.c \
    "$FW"/include/{fuji_mailbox,s2map,s2_cart,s2_text,fuji_load}.h "$GEN/fujiconfigrom.h"
cat "$FW/src/s2_text.c" "$FW/src/s2_font.c" > "$DIR/s2_text.cpp"
graft_fujins "$DIR" s2 fuji_load.cpp fuji_load.h -- \
    fuji_load_init fuji_load_target fuji_load_commit fuji_load_stage_config fuji_load_arm

python3 - "$DRV" <<'EOF2'
import sys
path = sys.argv[1]
text = open(path).read()
edits = [
    ('#include "video/cdp1861.h"\n',
     '#include "video/cdp1861.h"\n#include "studio2_fujinet.h"\n'),
    ('\tmap(0x0000, 0x01ff).mirror(0xfc00).ram();\n',
     '\tmap(0x0000, 0x01ff).mirror(0xfc00).ram().share("ram");\n'),
    ('\tBEEP(config, m_beeper, 300).add_route(ALL_OUTPUTS, "mono", 1.00);\n\n\tstudio2_cartslot(config);\n}\n',
     '\tBEEP(config, m_beeper, 300).add_route(ALL_OUTPUTS, "mono", 1.00);\n\n\tstudio2_cartslot(config);\n\n'
     '\t// FujiNet cartridge (pico/studio2 in fujinet-firmware): inert unless $FUJINET\n'
     '\tSTUDIO2_FUJINET(config, "fujinet", 0);\n}\n'),
]
changed = False
for old, new in edits:
    if new in text:
        continue
    if old not in text:
        sys.exit("apply.sh: studio2.cpp anchor not found: %r" % old.strip()[:60])
    text = text.replace(old, new, 1)
    changed = True
if changed:
    open(path, "w").write(text)
    print("apply.sh: studio2.cpp updated (build with REGENIE=1)")
EOF2

echo "apply.sh: done"
