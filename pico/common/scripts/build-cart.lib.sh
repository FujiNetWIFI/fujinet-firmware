# build-cart.lib.sh -- the part of every cart's build-cart.sh that is the same. A cart sets
# CART_BOARD (the default board), CART_ELF (the firmware's name), CART_BAKE (the headers
# --bake keeps) and cart_gen (writes them into build/gen), then sources this:
#
#   ./build-cart.sh [board]           cart_gen, then build
#   ./build-cart.sh --baked [board]   build from firmware/baked/ alone, with none of the
#                                     console's tools (CI, the ESP32 build)
#   ./build-cart.sh --bake [board]    as the first form, then copy CART_BAKE into
#                                     firmware/baked/

set -euo pipefail
cd "$(dirname "$0")"

MODE=gen
case "${1:-}" in
    --baked) MODE=baked; shift ;;
    --bake)  MODE=bake; shift ;;
esac
BOARD=${1:-$CART_BOARD}
GEN=$PWD/build/gen
OUT=build/fw-$BOARD

if [ "$MODE" = baked ]; then
    GEN=$PWD/build/none                 # firmware/baked/ is next on the path
else
    mkdir -p build/gen
    cart_gen
fi

export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
cmake -S firmware -B "$OUT" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD="$BOARD" -DFUJI_GEN="$GEN"
ninja -C "$OUT"
if [ -f tools/checksram.py ]; then
    python3 tools/checksram.py "$OUT/$CART_ELF.elf"
fi

if [ "$MODE" = bake ]; then
    mkdir -p firmware/baked
    for h in $CART_BAKE; do
        cp "build/gen/$h" firmware/baked/
    done
    echo "build-cart.sh: firmware/baked/ refreshed from build/gen"
fi
echo "build-cart.sh: $OUT/$CART_ELF.uf2"
