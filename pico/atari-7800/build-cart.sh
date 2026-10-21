#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else build/config.bin (the real CONFIG from
# fujinet-config/atari7800); else fujitest; else hello. The loader and the signed boot block
# come from ./build.sh, already in build/gen.
CART_BOARD=fuji7800_rp2354b
CART_ELF=fuji7800
CART_BAKE="fujiconfigrom.h a78loaderrom.h a78bootblk.h"

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
    for f in a78loaderrom.h a78bootblk.h; do
        [ -f "build/gen/$f" ] || { echo "build-cart.sh: build/gen/$f missing; run ./build.sh" >&2; exit 1; }
    done
    echo "build-cart.sh: baking build/$CLIENT.bin"
    python3 ../common/tools/mkromh.py --strip-a78 --claim=-0x90 \
        "build/$CLIENT.bin" > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
