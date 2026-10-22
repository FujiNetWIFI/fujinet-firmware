#!/usr/bin/env bash
# apply.sh -- graft the FujiNet SMS cartridge device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model (fujinet.cpp/h, common's fujitcp.c/h) and the cartridge
# firmware's own sources (fujimail, fuji_cfg, fuji_crc, fujibus, smsmap, fuji_load,
# the bus decode, the baked CONFIG and loader page) into src/devices/bus/sega8/, and
# edits:
#
#   scripts/src/bus.lua              the sega8 file list
#   src/devices/bus/sega8/sega8_slot.cpp  a selectable "fujinet" slot option
#   src/mame/sega/sms.cpp            a "none" BIOS on the export consoles, so
#                                    an image runs from power-on for A/B tests
#
# Idempotent: run it again after editing any source here. The CONFIG and
# loader headers come from build/gen (../build-cart.sh) or firmware/baked/.
#
# Afterwards: make -C /path/to/mame -j$(nproc) REGENIE=1 NOWERROR=1
# (REGENIE=1 whenever apply.sh says bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/sega8"
SLOT="$BUSDIR/sega8_slot.cpp"
DRIVER="$MAME/src/mame/sega/sms.cpp"
FW=../firmware
GEN=../build/gen
[ -f "$GEN/fujiconfigrom.h" ] || GEN=$FW/baked      # no fresh build: the baked pair

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }
for f in "$GEN/fujiconfigrom.h" "$GEN/smsloaderrom.h"; do
    [ -f "$f" ] || { echo "apply.sh: $f missing; run ../build-cart.sh" >&2; exit 1; }
done

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_cfg fuji_crc fuji_mailbox_proto \
    fuji_mailbox_check
graft "$BUSDIR" "$FW"/src/{smsmap,smsmap_db,fuji_load}.c \
    "$FW"/include/{fuji_mailbox,sms_cart,smsmap,smsmap_db,fuji_load}.h \
    "$GEN/fujiconfigrom.h" "$GEN/smsloaderrom.h"
graft_fujins "$BUSDIR" sms
graft_lua sega8 rom.h

if ! grep -q "SEGA8_FUJINET" "$SLOT"; then
    python3 - "$SLOT" <<'EOF2'
import sys
path = sys.argv[1]
text = open(path).read()
inc = '#include "mgear.h"\n'
if inc not in text:
    sys.exit("apply.sh: sega8_slot.cpp include anchor not found")
text = text.replace(inc, inc + '#include "fujinet.h"\n', 1)
opt = '// FujiNet cartridge (pico/sms in fujinet-firmware): selectable, any image\n\tdevice.option_add("fujinet",  SEGA8_FUJINET);\n'
for fn in ("void sms_cart(device_slot_interface &device)\n{\n",
           "void sg1000mk3_cart(device_slot_interface &device)\n{\n"):
    if fn not in text:
        sys.exit("apply.sh: sega8_slot.cpp option list not found: " + fn.split("(")[0])
    text = text.replace(fn, fn + "\t" + opt, 1)
open(path, "w").write(text)
EOF2
    echo "apply.sh: sega8_slot.cpp updated"
fi

if ! grep -q 'FujiNet A/B tests' "$DRIVER"; then
    python3 - "$DRIVER" <<'EOF2'
import re, sys
path = sys.argv[1]
text = open(path).read()
for name in ("sms1", "sms", "sms1pal", "smspal"):
    m = re.search(r"ROM_START\(\s*%s\s*\)(.*?)ROM_END" % name, text, re.S)
    if not m:
        sys.exit("apply.sh: sms.cpp ROM_START(%s) not found" % name)
    body = m.group(1)
    n = len(re.findall(r"ROM_SYSTEM_BIOS\(", body))
    add = '\tROM_SYSTEM_BIOS( %d, "none", "No BIOS (FujiNet A/B tests)" )\n' % n
    text = text[:m.end(1)] + add + text[m.end(1):]
open(path, "w").write(text)
EOF2
    echo "apply.sh: sms.cpp updated"
fi

echo "apply.sh: done"
