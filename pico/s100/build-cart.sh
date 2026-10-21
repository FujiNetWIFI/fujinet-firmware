#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B S-100 card firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# build.sh makes build/gen: the boot page, the loader, and CONFIG as image 0
# ($FUJI_CONFIG, the fujinet-config s100-rom build; else the BOOTCHECK stand-in).
CART_BOARD=fujis100_rp2354b
CART_ELF=fujis100
CART_BAKE="s100_bootpage.h s100_images.h"

cart_gen() {
    for h in s100_bootpage.h s100_images.h; do
        [ -f "build/gen/$h" ] || { echo "build-cart.sh: build/gen/$h missing; run ./build.sh first" >&2; exit 1; }
    done
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
