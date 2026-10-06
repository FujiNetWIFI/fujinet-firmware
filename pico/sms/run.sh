#!/usr/bin/env bash
# run.sh -- run an SMS client in MAME against a live fujinet-pc.
#
#   ./run.sh fujitest                 interactive
#   ./run.sh fujitest resettest       headless, driven by emu/resettest.lua
#   ./run.sh config cfgtest           the real CONFIG (fujinet-config sms/,
#                                     make install puts it in build/)
#   SYSTEM=smspal ./run.sh hello      another console (default sms1)
#   CART=/path/game.sms ./run.sh      any image, straight into the SRAM
#
# fujinet-pc must already be serving BoIP on 127.0.0.1:9995 (FUJINET_TCP
# overrides), and only one MAME may talk to it at a time. MAME must run from
# its own tree or -autoboot_script is silently ignored.
set -euo pipefail
cd "$(dirname "$0")"
HERE="$PWD"

MAME="${MAME:-$HOME/Workspace/mame}"
CLIENT="${1:-config}"
SCRIPT="${2:-}"
SYSTEM="${SYSTEM:-sms1}"

[ -x "$MAME/mame" ] || { echo "run.sh: no mame binary in $MAME" >&2; exit 1; }

export SMS_EMU_DIR="$HERE/emu"
args=("$SYSTEM" -slot fujinet -snapshot_directory "$HERE/build/snap"
      -nvram_directory "$HERE/build/nvram" -cfg_directory "$HERE/build/cfg")
[ -n "${BIOS:-}" ] && args+=(-bios "$BIOS")
if [ -n "${CART:-}" ]; then
    args+=(-cart "$CART")
else
    if [ ! -f "build/$CLIENT.sms" ]; then
        [ "$CLIENT" != config ] || { echo "run.sh: no build/config.sms; make install in fujinet-config sms/" >&2; exit 1; }
        ./build.sh "$CLIENT"
    fi
    args+=(-cart "$HERE/build/$CLIENT.sms")
fi

if [ -n "$SCRIPT" ]; then
    args+=(-autoboot_script "$HERE/emu/$SCRIPT.lua"
           -video none -sound none -nothrottle -seconds_to_run "${SECS:-120}")
else
    args+=(-window -nomax)
fi

cd "$MAME"
exec ./mame "${args[@]}"
