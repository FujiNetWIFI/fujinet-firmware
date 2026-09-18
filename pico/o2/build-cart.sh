#!/bin/bash
# Build the RP2040 cartridge firmware.
#
#   ./build-cart.sh              FujiNet build (default)
#   ./build-cart.sh --stock      stock PicoPAC, no mailbox
#
# Needs PICO_SDK_PATH, or a pico-sdk that cmake can find.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FUJI=ON
OUT="$HERE/firmware/build"
if [ "$1" = "--stock" ]; then
  FUJI=OFF
  OUT="$HERE/firmware/build-stock"
fi
cmake -B "$OUT" -S "$HERE/firmware" -DCMAKE_BUILD_TYPE=Release \
      -DCONFIG_FUJINET=$FUJI -G Ninja
ninja -C "$OUT"
arm-none-eabi-size "$OUT/PicoPAC_cart.elf"
echo "flash $OUT/PicoPAC_cart.uf2 in BOOTSEL mode"
