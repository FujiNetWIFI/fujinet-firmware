#!/usr/bin/env bash
# build-cart.sh -- build the S-100 card's RP2354B firmware.
#
#   ./build-cart.sh [board]           the boot page and images build.sh left in
#                                     build/gen (default board: fujis100_rp2354b)
#   ./build-cart.sh --baked [board]   from firmware/baked/ alone: no z88dk
#                                     needed (CI, the ESP32 build)
#   ./build-cart.sh --bake [board]    as the first form, then refresh
#                                     firmware/baked/ from build/gen
#
# build.sh makes build/gen: the boot page, the loader, and CONFIG as image 0
# ($FUJI_CONFIG, the fujinet-config s100-rom build; else the BOOTCHECK stand-in).
set -euo pipefail
cd "$(dirname "$0")"

MODE=gen
case "${1:-}" in
    --baked) MODE=baked; shift ;;
    --bake)  MODE=bake; shift ;;
esac
BOARD=${1:-fujis100_rp2354b}
GEN=$PWD/build/gen

if [ "$MODE" = baked ]; then
    GEN=$PWD/build/none                 # firmware/baked/ is next on the path
else
    for h in s100_bootpage.h s100_images.h; do
        [ -f "build/gen/$h" ] || { echo "build-cart.sh: build/gen/$h missing; run ./build.sh first" >&2; exit 1; }
    done
fi

export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
cmake -S firmware -B "build/fw-$BOARD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" -DFUJI_GEN="$GEN"
ninja -C "build/fw-$BOARD"
python3 tools/checksram.py "build/fw-$BOARD/fujis100.elf"

if [ "$MODE" = bake ]; then
    cp build/gen/s100_bootpage.h build/gen/s100_images.h firmware/baked/
    echo "build-cart.sh: firmware/baked/ refreshed from build/gen"
fi
echo "build-cart.sh: build/fw-$BOARD/fujis100.uf2"
