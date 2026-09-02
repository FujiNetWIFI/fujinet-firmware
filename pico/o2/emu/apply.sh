#!/bin/bash
# Apply the FujiNet model to a clean o2em checkout.
#
# The patch deliberately does not carry the shared sources. fujibus.[ch],
# fujimail.[ch], o2map.[ch] and fuji_mailbox.h are copied in from the cartridge
# firmware tree, so the emulator runs the cartridge's own wire codec, mailbox
# service and image mapper rather than a lookalike. What is left in the patch is
# only the port -- sockets instead of USB, an array instead of a live bus -- plus
# the headless harness.
set -e
O2EM="${1:-$HOME/Workspace/o2em}"
HERE="$(cd "$(dirname "$0")" && pwd)"

for f in include/fuji_mailbox.h include/fujibus.h include/fujimail.h \
         include/o2map.h src/fujibus.c src/fujimail.c src/o2map.c; do
  cp "$HERE/../firmware/$f" "$O2EM/"
done
git -C "$O2EM" apply "$HERE/o2em-fujinet.patch"

# o2em predates -fno-common (gcc 10+); several globals are tentative definitions
# in headers, so the link fails without -fcommon.
make -C "$O2EM" CFLAGS="-O2 -fcommon -I/usr/include"
echo "built $O2EM/o2em"
