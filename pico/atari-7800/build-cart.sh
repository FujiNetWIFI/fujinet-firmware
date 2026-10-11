#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware.
#
#   ./build-cart.sh [board]           bake build/<client>.bin, the loader and
#                                     the signed boot block into build/gen and
#                                     build (default board: fuji7800_rp2354b)
#   ./build-cart.sh --baked [board]   build from firmware/baked/ alone: no cc65
#                                     needed (CI, the ESP32 build)
#   ./build-cart.sh --bake [board]    as the first form, then refresh
#                                     firmware/baked/ from build/gen
#
# Client: $FUJI_CLIENT if set; else build/config.bin (the real CONFIG from
# fujinet-config/atari7800); else fujitest; else hello.
set -euo pipefail
cd "$(dirname "$0")"

MODE=gen
case "${1:-}" in
    --baked) MODE=baked; shift ;;
    --bake)  MODE=bake; shift ;;
esac
BOARD=${1:-fuji7800_rp2354b}
GEN=$PWD/build/gen

if [ "$MODE" = baked ]; then
    GEN=$PWD/build/none                 # firmware/baked/ is next on the path
else
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.bin ]; then
        CLIENT=config
    else
        CLIENT=fujitest
        [ -f build/fujitest.bin ] || CLIENT=hello
    fi
    [ -f "build/$CLIENT.bin" ] || { echo "build-cart.sh: build/$CLIENT.bin missing; run ./build.sh first" >&2; exit 1; }
    for f in a78loaderrom.h a78bootblk.h; do
        [ -f "build/gen/$f" ] || { echo "build-cart.sh: build/gen/$f missing; run ./build.sh" >&2; exit 1; }
    done
    echo "build-cart.sh: baking build/$CLIENT.bin"
    python3 tools/mkromh.py "build/$CLIENT.bin" > build/gen/fujiconfigrom.h
fi

export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
cmake -S firmware -B "build/fw-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" -DFUJI_GEN="$GEN"
ninja -C "build/fw-$BOARD"
python3 tools/checksram.py "build/fw-$BOARD/fuji7800.elf"

if [ "$MODE" = bake ]; then
    cp build/gen/fujiconfigrom.h build/gen/a78loaderrom.h build/gen/a78bootblk.h firmware/baked/
    echo "build-cart.sh: firmware/baked/ refreshed from build/$CLIENT.bin"
fi
echo "build-cart.sh: build/fw-$BOARD/fuji7800.uf2"
