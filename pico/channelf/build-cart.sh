#!/usr/bin/env bash
# build-cart.sh -- build the RP2040 Videocart firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else build/config.bin -- the REAL CONFIG from
# fujinet-config/channelf; else the fujicfg testrom stand-in; else fujitest; else hello. The
# stand-ins exist for bring-up only; a release cart bakes real CONFIG.
CART_BOARD=fujichannelf
CART_ELF=fujichannelf
CART_BAKE="fujiconfigrom.h"

cart_gen() {
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
    [ -f "build/$CLIENT.bin" ] || { echo "build-cart.sh: build/$CLIENT.bin missing; run ./build.sh first" >&2; exit 1; }
    echo "build-cart.sh: baking build/$CLIENT.bin"
    python3 ../common/tools/mkromh.py --blocks 1024 "build/$CLIENT.bin" > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
