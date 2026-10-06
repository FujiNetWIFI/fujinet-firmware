#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware.
#
#   ./build-cart.sh [board]           bake build/<client>.sms and build/loader.bin
#                                     into build/gen and build (default board:
#                                     fujisms_rp2354b)
#   ./build-cart.sh --baked [board]   build from firmware/baked/ alone: no Z80
#                                     toolchain needed (CI, the ESP32 build)
#   ./build-cart.sh --bake [board]    as the first form, then refresh
#                                     firmware/baked/ from build/gen
#
# Client: $FUJI_CLIENT if set; else build/config.sms (the real CONFIG from
# fujinet-config/sms); else fujitest; else hello.
set -euo pipefail
cd "$(dirname "$0")"

MODE=gen
case "${1:-}" in
    --baked) MODE=baked; shift ;;
    --bake)  MODE=bake; shift ;;
esac
BOARD=${1:-fujisms_rp2354b}
GEN=$PWD/build/gen

if [ "$MODE" = baked ]; then
    GEN=$PWD/build/none                 # firmware/baked/ is next on the path
else
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.sms ]; then
        CLIENT=config
    else
        CLIENT=fujitest
        [ -f build/fujitest.sms ] || CLIENT=hello
    fi
    [ -f "build/$CLIENT.sms" ] || { echo "build-cart.sh: build/$CLIENT.sms missing; run ./build.sh first" >&2; exit 1; }
    [ -f build/loader.bin ] || { echo "build-cart.sh: build/loader.bin missing; run ./build.sh loader" >&2; exit 1; }
    echo "build-cart.sh: baking build/$CLIENT.sms"
    mkdir -p build/gen
    python3 tools/mkromh.py "build/$CLIENT.sms" > build/gen/fujiconfigrom.h
    python3 tools/mkloaderh.py build/loader.bin > build/gen/smsloaderrom.h
fi

export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
cmake -S firmware -B "build/fw-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" -DFUJI_GEN="$GEN"
ninja -C "build/fw-$BOARD"
python3 tools/checksram.py "build/fw-$BOARD/fujisms.elf"

if [ "$MODE" = bake ]; then
    cp build/gen/fujiconfigrom.h build/gen/smsloaderrom.h firmware/baked/
    echo "build-cart.sh: firmware/baked/ refreshed from build/$CLIENT.sms"
fi
echo "build-cart.sh: build/fw-$BOARD/fujisms.uf2"
