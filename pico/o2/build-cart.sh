#!/bin/bash
# Build the RP2040 cartridge firmware.
#
#   ./build-cart.sh              FujiNet build (default)
#   ./build-cart.sh --stock      stock PicoPAC, no mailbox
#   ./build-cart.sh --bake       FujiNet build, then keep the CONFIG it baked:
#                                copy its header over firmware/include/fujiconfigrom.h
#
# The console client the cartridge serves at power-up, first found of:
#   $FUJI_CLIENT                 a path to a cart image
#   build/config.bin             the real CONFIG, as `make install` in fujinet-config/o2 leaves it
#   $FUJINET_CONFIG/o2/build/config.bin    (default ~/Workspace/fujinet-config)
#   firmware/include/fujiconfigrom.h       the committed one, when none of those exists
# A legacy 3K image or a protocol 2 (APPBANK) one; an APPBANK client is copied to RAM
# at boot, so keep it within 1K + 15 x 2K on the RP2040.
#
# Needs PICO_SDK_PATH, or a pico-sdk that cmake can find.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
FUJI=ON
BAKE=
OUT="$HERE/firmware/build"
for arg in "$@"; do
  case "$arg" in
    --stock) FUJI=OFF; OUT="$HERE/firmware/build-stock" ;;
    --bake) BAKE=1 ;;
    *) echo "usage: $0 [--stock] [--bake]" >&2; exit 1 ;;
  esac
done

if [ "$FUJI" = ON ]; then
  CLIENT="${FUJI_CLIENT:-}"
  for cand in "$HERE/build/config.bin" \
              "${FUJINET_CONFIG:-$HOME/Workspace/fujinet-config}/o2/build/config.bin"; do
    [ -z "$CLIENT" ] && [ -f "$cand" ] && CLIENT="$cand"
  done
  unset FUJI_GEN
  if [ -n "$CLIENT" ]; then
    [ -f "$CLIENT" ] || { echo "build-cart.sh: $CLIENT missing" >&2; exit 1; }
    echo "build-cart.sh: baking $CLIENT"
    # Refuse an image the cartridge could not map or that does not start with a JMP.
    make -C "$HERE/firmware/host_test" >/dev/null
    CHECK="$("$HERE/build/host_test/test_o2map" "$CLIENT")" || {
      echo "$CHECK" | tail -3 >&2
      exit 1
    }
    echo "$CHECK" | tail -3 | head -1
    mkdir -p "$HERE/build/gen"
    python3 "$HERE/../common/tools/mkromh.py" --blocks 1024 "$CLIENT" \
        > "$HERE/build/gen/fujiconfigrom.h"
    # CMakeLists.txt puts this directory ahead of firmware/include.
    export FUJI_GEN="$HERE/build/gen"
  else
    echo "build-cart.sh: no CONFIG found; baking firmware/include/fujiconfigrom.h"
  fi
fi

cmake -B "$OUT" -S "$HERE/firmware" -DCMAKE_BUILD_TYPE=Release \
      -DCONFIG_FUJINET=$FUJI -G Ninja
ninja -C "$OUT"
arm-none-eabi-size "$OUT/PicoPAC_cart.elf"
if [ -n "$BAKE" ]; then
  [ -f "$HERE/build/gen/fujiconfigrom.h" ] || { echo "build-cart.sh: --bake with no CONFIG to bake" >&2; exit 1; }
  cp "$HERE/build/gen/fujiconfigrom.h" "$HERE/firmware/include/fujiconfigrom.h"
  echo "build-cart.sh: firmware/include/fujiconfigrom.h refreshed"
fi
echo "flash $OUT/PicoPAC_cart.uf2 in BOOTSEL mode"
