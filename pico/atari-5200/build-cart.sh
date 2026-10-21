#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else build/config.bin (the real CONFIG from
# fujinet-config/atari5200); else fujitest; else hello.
CART_BOARD=fuji5200_rp2354b
CART_ELF=fuji5200
CART_BAKE="fujiconfigrom.h"

cart_gen() {
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.bin ]; then
        CLIENT=config
    else
        CLIENT=fujitest
        [ -f build/fujitest.bin ] || CLIENT=hello
    fi
    [ -f "build/$CLIENT.bin" ] || { echo "build-cart.sh: build/$CLIENT.bin missing; run ./build.sh first" >&2; exit 1; }
    echo "build-cart.sh: baking build/$CLIENT.bin"
    python3 tools/mkromh.py "build/$CLIENT.bin" > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
