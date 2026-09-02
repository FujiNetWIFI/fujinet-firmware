#!/bin/bash
# Apply the FujiNet model to a clean o2em checkout.
#
# The patch deliberately does not carry fujibus.[ch] or fuji_mailbox.h: those are
# copied in from the cart firmware tree so the emulator and the RP2040 share one
# codec and one mailbox definition, and cannot drift.
set -e
O2EM="${1:-$HOME/Workspace/o2em}"
HERE="$(cd "$(dirname "$0")" && pwd)"

cp "$HERE/../firmware/include/fuji_mailbox.h" "$O2EM/"
cp "$HERE/../../intellivision/firmware/include/fujibus.h" "$O2EM/"
cp "$HERE/../../intellivision/firmware/src/fujibus.c" "$O2EM/"
git -C "$O2EM" apply "$HERE/o2em-fujinet.patch"

# o2em predates -fno-common (gcc 10+); several globals are tentative definitions
# in headers, so the link fails without -fcommon.
make -C "$O2EM" CFLAGS="-O2 -fcommon -I/usr/include"
echo "built $O2EM/o2em"
