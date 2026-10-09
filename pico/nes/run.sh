#!/usr/bin/env bash
# run.sh -- run an NES client in MAME against a live fujinet-pc.
#
#   ./run.sh fujitest                 interactive, the image runs directly
#   ./run.sh fujiboot boottest        headless, driven by emu/boottest.lua
#   FUJINET_LOADER=1 ./run.sh hello   send the -cart image through the loader ROM,
#                                     as the cart does at power-on
#
# MAME's nes driver insists on a cartridge image, so there is always a -cart;
# the device treats it as the resident image the cart would have baked in.
#   MAME=~/src/mame ./run.sh hello    a different MAME tree
#
# fujinet-pc must already be running with BoIP on 127.0.0.1:9995 (FUJINET_TCP
# overrides). MAME must run from its own tree or -autoboot_script is silently
# ignored, so everything below is passed as an absolute path.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame}"
CLIENT="${1:-fujitest}"
SCRIPT="${2:-}"

[ -x "$MAME/mame" ] || { echo "run.sh: no mame binary in $MAME" >&2; exit 1; }

export NES_EMU_DIR="$HERE/emu"
[ -f "build/$CLIENT.nes" ] || ./build.sh "$CLIENT"
args=(nes -nes_slot fujinet -cart "$HERE/build/$CLIENT.nes"
      -snapshot_directory "$HERE/build/snap")

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle -seconds_to_run "${SECS:-60}")
else
    args+=(-window -nomax)
fi

cd "$MAME"
exec ./mame "${args[@]}"
