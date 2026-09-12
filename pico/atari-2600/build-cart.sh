#!/usr/bin/env bash
# build-cart.sh -- build the RP2040 cartridge firmware with a client baked in.
#
#   ./build-cart.sh [client]        default: fujidir
#
# The client is the boot ROM: at power-up there is no network and nothing to
# load one from, so it is compiled into the firmware image (tools/mkromh.py)
# and served the instant the console starts fetching.

set -euo pipefail
cd "$(dirname "$0")"

CLIENT=${1:-fujidir}
BUILD=firmware/build-fujivcs

./build.sh "$CLIENT" >/dev/null
python3 tools/mkromh.py "build/$CLIENT.bin" > firmware/include/fujiconfigrom.h
echo "baked build/$CLIENT.bin ($(stat -c%s "build/$CLIENT.bin") bytes) into the firmware"

cmake -S firmware -B "$BUILD" -DPICO_BOARD=fujivcs >/dev/null
cmake --build "$BUILD" -j"$(nproc)"

# The bus loop must not be able to fault to flash. This is an assertion, not
# a guideline -- see tools/checksram.py.
python3 tools/checksram.py "$BUILD/fujivcs.elf"

echo
ls -l "$BUILD/fujivcs.uf2"
arm-none-eabi-size "$BUILD/fujivcs.elf"
