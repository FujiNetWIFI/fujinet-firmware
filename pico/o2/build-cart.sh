#!/bin/bash
# Build the cartridge firmware.
#
#   ./build-cart.sh                         FujiNet build for PicoPAC's RP2040 (default)
#   ./build-cart.sh --stock                 stock PicoPAC, no mailbox
#   ./build-cart.sh --board fujio2_rp2354b  FujiNet build for the FujiNet-O2 Rev0 cart
#
# Needs PICO_SDK_PATH, or a pico-sdk that cmake can find.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FUJI=ON
BOARD=
OUT="$HERE/firmware/build"
while [ $# -gt 0 ]; do
  case "$1" in
    --stock) FUJI=OFF; OUT="$HERE/firmware/build-stock" ;;
    --board) BOARD="$2"; OUT="$HERE/firmware/build-$2"; shift ;;
    *) echo "usage: $0 [--stock] [--board NAME]" >&2; exit 1 ;;
  esac
  shift
done
cmake -B "$OUT" -S "$HERE/firmware" -DCMAKE_BUILD_TYPE=Release \
      -DCONFIG_FUJINET=$FUJI ${BOARD:+-DPICO_BOARD=$BOARD} -G Ninja
ninja -C "$OUT"
arm-none-eabi-size "$OUT/PicoPAC_cart.elf"
echo "flash $OUT/PicoPAC_cart.uf2 in BOOTSEL mode"
