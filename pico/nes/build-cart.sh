#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware.
#
# Usage: ./build-cart.sh [board]          default board: fujines_rp2354b
#
# Regenerates fujiconfigrom.h and nesloaderrom.h, then builds with the pico-sdk.
# Baked client: $FUJI_CLIENT, else build/config.nes (fujinet-config/nes), else
# the bring-up stand-ins fujicfg, fujitest, hello.
set -euo pipefail
cd "$(dirname "$0")"

BOARD=${1:-fujines_rp2354b}
if [ -n "${FUJI_CLIENT:-}" ]; then
    CLIENT=$FUJI_CLIENT
elif [ -f build/config.nes ]; then
    CLIENT=config
else
    CLIENT=fujicfg
fi
if [ ! -f "build/$CLIENT.nes" ] && [ "$CLIENT" = fujicfg ]; then
    for fallback in fujitest hello; do
        if [ -f "build/$fallback.nes" ]; then
            echo "build-cart.sh: no fujicfg.nes yet; baking $fallback instead" >&2
            CLIENT=$fallback
            break
        fi
    done
fi
[ -f "build/$CLIENT.nes" ] || { echo "build-cart.sh: build/$CLIENT.nes missing; run ./build.sh first" >&2; exit 1; }
[ -f build/loader.bin ] || { echo "build-cart.sh: build/loader.bin missing; run ./build.sh loader" >&2; exit 1; }

echo "build-cart.sh: baking build/$CLIENT.nes"
python3 tools/mkromh.py "build/$CLIENT.nes" > firmware/include/fujiconfigrom.h
python3 tools/mkloaderh.py build/loader.bin > firmware/include/nesloaderrom.h
for h in fujiconfigrom nesloaderrom; do
    grep -q "^static const unsigned char" "firmware/include/$h.h" \
        || { echo "build-cart.sh: firmware/include/$h.h was not generated" >&2; exit 1; }
done

export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
cmake -S firmware -B "firmware/build-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD"
ninja -C "firmware/build-$BOARD"
python3 tools/checksram.py "firmware/build-$BOARD/fujines.elf"
echo "build-cart.sh: firmware/build-$BOARD/fujines.uf2"
