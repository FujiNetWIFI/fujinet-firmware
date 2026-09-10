#!/usr/bin/env bash
# build-cart.sh -- build the RP2040 Videocart firmware.
#
# Usage: ./build-cart.sh [board]          default board: fujichannelf
#
# Regenerates firmware/include/fujiconfigrom.h from the baked-in client first,
# then a plain pico-sdk CMake build. Client resolution order: $FUJI_CLIENT if
# set; else build/config.bin -- the REAL CONFIG from fujinet-config/channelf;
# else the fujicfg testrom stand-in; else fujitest. The stand-ins exist for
# bring-up only; a release cart bakes real CONFIG.
set -euo pipefail
cd "$(dirname "$0")"

BOARD=${1:-fujichannelf}
if [ -n "${FUJI_CLIENT:-}" ]; then
    CLIENT=$FUJI_CLIENT
elif [ -f build/config.bin ]; then
    CLIENT=config
else
    CLIENT=fujicfg
fi

if [ ! -f "build/$CLIENT.bin" ] && [ "$CLIENT" = fujicfg ]; then
    for fallback in fujitest hello; do
        if [ -f "build/$fallback.bin" ]; then
            echo "build-cart.sh: no fujicfg.bin yet; baking $fallback instead" >&2
            CLIENT=$fallback
            break
        fi
    done
fi
if [ ! -f "build/$CLIENT.bin" ]; then
    echo "build-cart.sh: build/$CLIENT.bin missing; run ./build.sh first" >&2
    exit 1
fi

echo "build-cart.sh: baking build/$CLIENT.bin"
python3 tools/mkromh.py "build/$CLIENT.bin" > firmware/include/fujiconfigrom.h

cmake -S firmware -B "firmware/build-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD"
ninja -C "firmware/build-$BOARD"
echo "build-cart.sh: firmware/build-$BOARD/fujichannelf.uf2"
