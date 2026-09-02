#!/bin/bash
# Apply the FujiNet model to a clean o2em checkout.
#
# The patch deliberately does not carry the shared sources: fujibus.[ch],
# o2map.[ch] and fuji_mailbox.h are copied in from the cartridge firmware tree so
# the emulator and the RP2040 share one wire codec, one image mapper and one
# mailbox definition, and cannot drift on any of them.
set -e
O2EM="${1:-$HOME/Workspace/o2em}"
HERE="$(cd "$(dirname "$0")" && pwd)"

for f in include/fuji_mailbox.h include/fujibus.h include/o2map.h \
         src/fujibus.c src/o2map.c; do
  cp "$HERE/../firmware/$f" "$O2EM/"
done
git -C "$O2EM" apply "$HERE/o2em-fujinet.patch"

# o2em predates -fno-common (gcc 10+); several globals are tentative definitions
# in headers, so the link fails without -fcommon.
make -C "$O2EM" CFLAGS="-O2 -fcommon -I/usr/include"
echo "built $O2EM/o2em"
