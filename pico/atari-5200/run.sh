#!/usr/bin/env bash
# run.sh -- run a 5200 client in MAME against a live fujinet-pc.
#
#   ./run.sh fujitest                 interactive
#   ./run.sh fujitest shot            headless, driven by emu/shot.lua
#   ./run.sh config cfgtest           the real CONFIG (fujinet-config
#                                     atari5200/, make install puts it in build/)
#   SYSTEM=a5200a ./run.sh hello      the 2-port console (default a5200)
#   IMAGE=/path/game.a52 ./run.sh -   any image, served from power-on
#
# fujinet-pc must already be serving BoIP on 127.0.0.1:9995 (FUJINET_TCP
# overrides), and only one MAME may talk to it at a time. MAME is the
# single-driver build in $MAME (default ~/Workspace/mame-a5200, binary
# ./a5200); it must run from its own tree or -autoboot_script is ignored.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame-a5200}"
BIN="${MAMEBIN:-./a5200}"
ROMS="${ROMS:-$HOME/Workspace/mame/roms}"
CLIENT="${1:-config}"
SCRIPT="${2:-}"
SYSTEM="${SYSTEM:-a5200}"

[ -x "$MAME/$BIN" ] || { echo "run.sh: no $BIN in $MAME" >&2; exit 1; }

export A52_EMU_DIR="$HERE/emu"
[ "$CLIENT" = config ] && export A52_SCREEN="${A52_SCREEN:-0x3C00}"
[ -n "${IMAGE:-}" ] && export FUJINET_IMAGE="$IMAGE"
mkdir -p build/snap build/nvram build/cfg
args=("$SYSTEM" -rompath "$ROMS" -cartslot fujinet -snapshot_directory "$HERE/build/snap"
      -nvram_directory "$HERE/build/nvram" -cfg_directory "$HERE/build/cfg" -skip_gameinfo)
[ -n "${BIOS:-}" ] && args+=(-bios "$BIOS")
if [ "$CLIENT" != - ]; then
    if [ ! -f "build/$CLIENT.bin" ]; then
        [ "$CLIENT" != config ] || { echo "run.sh: no build/config.bin; make install in fujinet-config atari5200/" >&2; exit 1; }
        ./build.sh "$CLIENT"
    fi
    args+=(-cart "$HERE/build/$CLIENT.bin")
fi

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle -seconds_to_run "${SECS:-120}")
else
    args+=(-window -nomax)
fi

cd "$MAME"
exec "$BIN" "${args[@]}"
