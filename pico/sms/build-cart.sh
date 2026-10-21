#!/usr/bin/env bash
# build-cart.sh -- build the RP2354B cartridge firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else build/config.sms (the real CONFIG from
# fujinet-config/sms); else fujitest; else hello. build/loader.bin is baked beside it.
CART_BOARD=fujisms_rp2354b
CART_ELF=fujisms
CART_BAKE="fujiconfigrom.h smsloaderrom.h"

cart_gen() {
    if [ -n "${FUJI_CLIENT:-}" ]; then
        CLIENT=$FUJI_CLIENT
    elif [ -f build/config.sms ]; then
        CLIENT=config
    else
        CLIENT=fujitest
        [ -f build/fujitest.sms ] || CLIENT=hello
    fi
    [ -f "build/$CLIENT.sms" ] || { echo "build-cart.sh: build/$CLIENT.sms missing; run ./build.sh first" >&2; exit 1; }
    [ -f build/loader.bin ] || { echo "build-cart.sh: build/loader.bin missing; run ./build.sh loader" >&2; exit 1; }
    echo "build-cart.sh: baking build/$CLIENT.sms"
    python3 ../common/tools/mkromh.py --size 0x8000 --expect "0x7FF0:TMR SEGA" --claim 0x7FDC \
        "build/$CLIENT.sms" > build/gen/fujiconfigrom.h
    python3 tools/mkloaderh.py build/loader.bin > build/gen/smsloaderrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
