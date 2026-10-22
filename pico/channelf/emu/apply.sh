#!/usr/bin/env bash
# apply.sh -- graft the FujiNet Videocart device into a MAME tree.
#
# Usage: ./apply.sh /path/to/mame
#
# Copies the device model (fujinet.cpp/h, common's fujitcp.c/h) and the cartridge
# firmware's own protocol sources (fujimail, fujibus, chfmap, the bus observer
# and their headers -- MAME then runs the code the cartridge actually ships)
# into src/devices/bus/chanf/, and makes the shared-file edits:
#
#   scripts/src/bus.lua          the CHANNELF file list
#   src/devices/bus/chanf/slot.h   a CF_FUJINET pcb type
#   src/devices/bus/chanf/slot.cpp select it when our device is the one loaded
#   src/mame/fairchild/channelf.cpp  the slot option, and a write handler
#   src/devices/cpu/f8/f8.cpp      the ROMC trace hook (via f8trace.py)
#
# Idempotent: run it again after editing any source here.
#
# Afterwards: make -C /path/to/mame -j$(nproc) REGENIE=1 (REGENIE=1 whenever
# apply.sh says bus.lua changed).

set -euo pipefail
cd "$(dirname "$0")"

MAME=${1:?usage: apply.sh /path/to/mame}
BUSDIR="$MAME/src/devices/bus/chanf"
SLOT_H="$BUSDIR/slot.h"
SLOT_C="$BUSDIR/slot.cpp"
DRIVER="$MAME/src/mame/fairchild/channelf.cpp"

[ -d "$BUSDIR" ] || { echo "apply.sh: $BUSDIR missing" >&2; exit 1; }

. ../../common/emu/graft.lib.sh

graft "$BUSDIR" fujinet.cpp fujinet.h
graft_common "$BUSDIR" fujitcp fujimail fujibus fuji_mailbox_proto fuji_mailbox_check
graft "$BUSDIR" ../firmware/src/chfmap.c ../firmware/include/{chfmap,fuji_mailbox,channelf_cart}.h
graft_fujins "$BUSDIR" channelf
graft_lua chanf rom.h

# slot.h: a pcb type of our own.
if ! grep -q "CF_FUJINET" "$SLOT_H"; then
    python3 - "$SLOT_H" <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()
anchor = "\tCF_MULTI\n};"
if anchor not in text:
    sys.exit("apply.sh: slot.h pcb enum anchor not found")
open(path, "w").write(text.replace(anchor, "\tCF_MULTI,\n\tCF_FUJINET\n};", 1))
EOF
    echo "apply.sh: slot.h updated"
fi

# slot.cpp: register the type, and pick it when OUR device is in the slot.
#
# call_load() forces CF_CHESS for any fullpath image (it defaults to the Saba
# Schach PCB because homebrew tends to expect RAM at $2800), which would install
# the console's own RAM handlers over part of our address space. Keying on the
# loaded device type instead of the file is the only way to tell it apart.
if ! grep -q "CF_FUJINET" "$SLOT_C"; then
    python3 - "$SLOT_C" <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()

inc = '#include "slot.h"\n'
if inc not in text:
    sys.exit("apply.sh: slot.cpp include anchor not found")
text = text.replace(inc, inc + '#include "fujinet.h"\n', 1)

lst = '\t{ CF_MULTI,    "multi" }\n'
if lst not in text:
    sys.exit("apply.sh: slot.cpp slot_list anchor not found")
text = text.replace(lst, '\t{ CF_MULTI,    "multi" },\n\t{ CF_FUJINET,  "fujinet" }\n', 1)

old = """			if (len == 0x40000)
				m_type = CF_MULTI;  // TODO1: differentiate multicart final and earlier from fullpath
			else
				m_type = CF_CHESS;  // TODO2: is there any way to detect Maze and Hangman from fullpath?"""
if old not in text:
    sys.exit("apply.sh: slot.cpp call_load anchor not found")
new = """			if (m_cart->device().type() == CHANF_ROM_FUJINET)
				m_type = CF_FUJINET;
			else if (len == 0x40000)
				m_type = CF_MULTI;  // TODO1: differentiate multicart final and earlier from fullpath
			else
				m_type = CF_CHESS;  // TODO2: is there any way to detect Maze and Hangman from fullpath?"""
text = text.replace(old, new, 1)
open(path, "w").write(text)
EOF
    echo "apply.sh: slot.cpp updated"
fi

# channelf.cpp: include, a selectable slot option, and the write handler.
#
# The driver maps $0800-$FFFF read-only, because no stock Videocart needs more.
# Ours does: ROMC 05 is a real store cycle and both the RAM arena and the whole
# console -> cart direction ride it. Reads stay on read_rom, so the offsets
# match ($0800-relative in both directions).
if ! grep -q "CHANF_ROM_FUJINET" "$DRIVER"; then
    python3 - "$DRIVER" <<'EOF'
import sys
path = sys.argv[1]
text = open(path).read()

inc_anchor = '#include "bus/chanf/slot.h"\n'
if inc_anchor not in text:
    # the driver includes its own header, which pulls the slot in
    inc_anchor = '#include "channelf.h"\n'
    if inc_anchor not in text:
        sys.exit("apply.sh: channelf.cpp include anchor not found")
text = text.replace(inc_anchor, inc_anchor + '#include "bus/chanf/fujinet.h"\n', 1)

opt_anchor = '\tdevice.option_add_internal("multi",    CHANF_ROM_MULTI_FINAL);\n'
if opt_anchor not in text:
    sys.exit("apply.sh: channelf.cpp option anchor not found")
text = text.replace(opt_anchor,
                    opt_anchor + '\tdevice.option_add("fujinet", CHANF_ROM_FUJINET);\n',
                    1)

ms_anchor = """			case CF_MULTI:
			case CF_MULTI_OLD:"""
if ms_anchor not in text:
    sys.exit("apply.sh: channelf.cpp machine_start anchor not found")
text = text.replace(ms_anchor, """			case CF_FUJINET:
				m_maincpu->space(AS_PROGRAM).install_write_handler(0x0800, 0xffff, write8sm_delegate(*m_cart, FUNC(channelf_cart_slot_device::write_ram)));
				break;
			case CF_MULTI:
			case CF_MULTI_OLD:""", 1)
open(path, "w").write(text)
EOF
    echo "apply.sh: channelf.cpp updated"
fi

# The ROMC trace hook, for firmware/host_test/test_romc.c.
python3 f8trace.py "$MAME"

echo "apply.sh: done"
