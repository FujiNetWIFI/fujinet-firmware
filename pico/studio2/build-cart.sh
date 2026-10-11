#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware.
#
#   ./build-cart.sh [board]           bake build/<client>.st2 into build/gen
#                                     and build (default board: fujistudio2_rp2354b)
#   ./build-cart.sh --baked [board]   build from firmware/baked/ alone: no AS
#                                     needed (CI, the ESP32 build)
#   ./build-cart.sh --bake [board]    as the first form, then refresh
#                                     firmware/baked/ from build/gen
#
# Client: $FUJI_CLIENT if set; else build/config.st2 (the real CONFIG from
# fujinet-config/studio2); else fujitest; else hello.
set -euo pipefail
cd "$(dirname "$0")"

MODE=gen
case "${1:-}" in
    --baked) MODE=baked; shift ;;
    --bake)  MODE=bake; shift ;;
esac
BOARD=${1:-fujistudio2_rp2354b}
GEN=$PWD/build/gen

if [ "$MODE" = baked ]; then
    GEN=$PWD/build/none                 # firmware/baked/ is next on the path
else
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.st2 ]; then
        CLIENT=config
    else
        CLIENT=fujitest
        [ -f build/fujitest.st2 ] || CLIENT=hello
    fi
    [ -f "build/$CLIENT.st2" ] || { echo "build-cart.sh: build/$CLIENT.st2 missing; run ./build.sh first" >&2; exit 1; }
    mkdir -p build/gen
    echo "build-cart.sh: baking build/$CLIENT.st2"
    python3 tools/mkromh.py "build/$CLIENT.st2" > build/gen/fujiconfigrom.h
fi

export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
cmake -S firmware -B "build/fw-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" -DFUJI_GEN="$GEN"
ninja -C "build/fw-$BOARD"
python3 tools/checksram.py "build/fw-$BOARD/fujistudio2.elf"

if [ "$MODE" = bake ]; then
    mkdir -p firmware/baked
    cp build/gen/fujiconfigrom.h firmware/baked/
    echo "build-cart.sh: firmware/baked/ refreshed from build/$CLIENT.st2"
fi
echo "build-cart.sh: build/fw-$BOARD/fujistudio2.uf2"
