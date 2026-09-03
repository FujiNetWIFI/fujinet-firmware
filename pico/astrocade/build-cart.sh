#!/usr/bin/env bash
# build-cart.sh -- build the RP2040 cartridge firmware.
#
# Usage: ./build-cart.sh [board]          default board: fujicade
#
# Regenerates firmware/include/fujiconfigrom.h from the baked-in client
# first (FUJI_CLIENT, default fujicfg, falling back to fujitest while the
# CONFIG client is not built), then a plain pico-sdk CMake build.

set -euo pipefail
cd "$(dirname "$0")"

BOARD=${1:-fujicade}
CLIENT=${FUJI_CLIENT:-fujicfg}

if [ ! -f "build/$CLIENT.bin" ]; then
    if [ "$CLIENT" = fujicfg ] && [ -f build/fujitest.bin ]; then
        echo "build-cart.sh: no fujicfg.bin yet; baking fujitest instead" >&2
        CLIENT=fujitest
    else
        echo "build-cart.sh: build/$CLIENT.bin missing; run ./build.sh first" >&2
        exit 1
    fi
fi
python3 tools/mkromh.py "build/$CLIENT.bin" > firmware/include/fujiconfigrom.h

cmake -S firmware -B "firmware/build-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD"
ninja -C "firmware/build-$BOARD"
echo "build-cart.sh: firmware/build-$BOARD/fujicade.uf2"
