#!/usr/bin/env bash
# apply.sh /path/to/mame -- graft the FujiNet Model I card into a MAME tree as the
# trs80fn machine: the card's own sources and pico/common's go into src/mame/trs/,
# and trs80.cpp gains one include at its end. Idempotent.
# Then: make -C /path/to/mame SUBTARGET=trs80 SOURCES=src/mame/trs/trs80.cpp REGENIE=1
# (only ever in a tree of its own: SOURCES= rebuilds the whole tree's project files).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
TRSDIR="$MAME/src/mame/trs"
DRIVER="$TRSDIR/trs80.cpp"
FW=../firmware

[ -f "$DRIVER" ] || { echo "apply.sh: $DRIVER missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$TRSDIR" trs80m1_fujinet.cpp trs80m1_fujinet.h trs80fn.ipp
graft_common "$TRSDIR" fujibus fuji_ring fuji_pipe fuji_iolink fuji_tcache
graft "$TRSDIR" "$FW"/src/m1_{card,wd1771,uart}.c \
    "$FW"/include/{m1_card,m1_wd1771,m1_uart,fuji_tcache_cfg}.h
graft_fujins "$TRSDIR" m1 m1_card.cpp m1_wd1771.cpp m1_uart.cpp m1_card.h m1_wd1771.h \
    m1_uart.h trs80m1_fujinet.cpp -- fuji_tcache_accept

if ! grep -q 'trs80fn.ipp' "$DRIVER"; then
    printf '\n#include "trs80fn.ipp"\n' >> "$DRIVER"
    echo "apply.sh: trs80.cpp updated"
fi
grep -q '^trs80fn$' "$MAME/src/mame/mame.lst" || sed -i '/^trs80l2$/a trs80fn' "$MAME/src/mame/mame.lst"

echo "apply.sh: done"
