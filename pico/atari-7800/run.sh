#!/usr/bin/env bash
# run.sh -- run a 7800 client in MAME against a live fujinet-pc.
#
#   ./run.sh fujitest                 interactive
#   ./run.sh fujitest shot            headless, driven by emu/shot.lua
#   ./run.sh config cfgtest           the real CONFIG (fujinet-config
#                                     atari7800/, make install puts it in build/)
#   SYSTEM=a7800p ./run.sh hello      a PAL console (default a7800, NTSC)
#   IMAGE=/path/game.bin ./run.sh     any image, straight into the SRAM
#   HSC=/path/hsc.bin ./run.sh ...    with the High Score Cart installed
#
# fujinet-pc must already be serving BoIP on 127.0.0.1:9995 (FUJINET_TCP
# overrides), and only one MAME may talk to it at a time. MAME is the
# single-driver build in $MAME (default ~/Workspace/mame-a7800, binary
# ./a7800); it must run from its own tree or -autoboot_script is ignored.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame-a7800}"
BIN="${MAMEBIN:-./a7800}"
ROMS="${ROMS:-$HOME/Workspace/mame/roms}"
CLIENT="${1:-config}"
SCRIPT="${2:-}"
SYSTEM="${SYSTEM:-a7800}"

[ -x "$MAME/$BIN" ] || { echo "run.sh: no $BIN in $MAME" >&2; exit 1; }

export A78_EMU_DIR="$HERE/emu"
[ -n "${IMAGE:-}" ] && export FUJINET_IMAGE="$IMAGE"
[ -n "${HSC:-}" ] && export FUJINET_HSC="$HSC"
mkdir -p build/snap build/nvram build/cfg
args=("$SYSTEM" -rompath "$ROMS" -cartslot fujinet -snapshot_directory "$HERE/build/snap"
      -nvram_directory "$HERE/build/nvram" -cfg_directory "$HERE/build/cfg")
[ -n "${BIOS:-}" ] && args+=(-bios "$BIOS")
if [ ! -f "build/$CLIENT.a78" ]; then
    [ "$CLIENT" != config ] || { echo "run.sh: no build/config.a78; make install in fujinet-config atari7800/" >&2; exit 1; }
    ./build.sh "$CLIENT"
fi
args+=(-cart "$HERE/build/$CLIENT.a78")

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle -seconds_to_run "${SECS:-120}")
else
    args+=(-window -nomax)
fi

cd "$MAME"
exec "$BIN" "${args[@]}"
