#!/usr/bin/env bash
# build-cart.sh -- build the template cartridge's firmware: [--baked | --bake] [board], as
# pico/common/scripts/build-cart.lib.sh describes.
#
# Client: $FUJI_CLIENT if set; else a stand-in, 8K of $FF carrying only the mailbox claim,
# which links and paints the mailbox but runs nothing. A real cart bakes its CONFIG here.
CART_BOARD=fujitemplate_rp2354b
CART_ELF=fujitemplate
CART_BAKE="fujiconfigrom.h"

cart_gen() {
    local client=${FUJI_CLIENT:-build/standin.bin}

    if [ -z "${FUJI_CLIENT:-}" ]; then
        python3 -c 'import sys
img = bytearray(b"\xff" * 0x2000)
img[0x1CFC:0x1D00] = b"FUJI"
open(sys.argv[1], "wb").write(img)' "$client"
    fi
    echo "build-cart.sh: baking $client"
    python3 ../common/tools/mkromh.py --size 8192 --claim 0x1CFC "$client" \
        > build/gen/fujiconfigrom.h
}

. "$(dirname "$0")/../common/scripts/build-cart.lib.sh"
