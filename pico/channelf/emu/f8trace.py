#!/usr/bin/env python3
"""Patch a MAME tree's F8 core to emit a golden ROMC trace.

MAME does not model the ROMC bus -- its F8 core keeps one internal set of
PC0/PC1/DC0/DC1 and calls ROMC_xx() directly, and a cart device only ever sees
flat read/write. That means nothing in the MAME harness exercises the cart's
ROMC state machine, which is the one genuinely novel part of this port.

So we make MAME tell us what it did: a record per bus cycle, which
test_romc.c replays through chf_bus_cycle() and compares. The trace is the
CPU's own register state, which is exactly what every device on a real F8 bus
is required to track in lockstep.

Idempotent, and reversible with --revert (a .fujibak is kept).
Record: romc u8, dbus u8, pc0 u16le, pc1 u16le, dc0 u16le, dc1 u16le = 10 bytes.
"""
import os
import re
import sys

MARK = "/* --- FujiNet ROMC trace (pico/channelf) --- */"

TRACER = MARK + r'''
#include <cstdio>
#include <cstdlib>
namespace {
FILE *s_fuji_trace = nullptr;
bool  s_fuji_trace_tried = false;
void f8_fujitrace(u8 romc, u8 dbus, u16 pc0, u16 pc1, u16 dc0, u16 dc1)
{
	if (!s_fuji_trace_tried)
	{
		s_fuji_trace_tried = true;
		const char *p = getenv("F8_ROMC_TRACE");
		if (p && *p) s_fuji_trace = fopen(p, "wb");
	}
	if (!s_fuji_trace) return;
	unsigned char r[10];
	r[0] = romc;      r[1] = dbus;
	r[2] = pc0 & 0xff; r[3] = (pc0 >> 8) & 0xff;
	r[4] = pc1 & 0xff; r[5] = (pc1 >> 8) & 0xff;
	r[6] = dc0 & 0xff; r[7] = (dc0 >> 8) & 0xff;
	r[8] = dc1 & 0xff; r[9] = (dc1 >> 8) & 0xff;
	fwrite(r, 1, sizeof r, s_fuji_trace);
	fflush(s_fuji_trace);
}
} // anonymous namespace
'''

FUNC = re.compile(
    r'(void f8_cpu_device::ROMC_([0-9A-F]{2})\([^)]*\)\s*\{)(.*?)(\n\})',
    re.DOTALL)
ICOUNT = re.compile(r'(\n\t)(m_icount -= [^;]*;)')


def patch(path):
    src = open(path).read()
    if MARK in src:
        print("f8trace: already patched: %s" % path)
        return 0
    if not os.path.exists(path + ".fujibak"):
        open(path + ".fujibak", "w").write(src)

    anchor = "void f8_cpu_device::ROMC_00("
    if anchor not in src:
        sys.exit("f8trace: cannot find ROMC_00 in %s" % path)
    src = src.replace(anchor, TRACER + "\n" + anchor, 1)

    n = [0]

    def do(mo):
        head, num, body, tail = mo.group(1), mo.group(2), mo.group(3), mo.group(4)
        call = "\\1f8_fujitrace(0x%s, m_dbus, m_pc0, m_pc1, m_dc0, m_dc1);\\1\\2" % num
        body2, k = ICOUNT.subn(call, body, count=1)
        if k:
            n[0] += 1
        return head + body2 + tail

    src = FUNC.sub(do, src)
    if n[0] != 32:
        sys.exit("f8trace: instrumented %d ROMC states, expected 32" % n[0])
    open(path, "w").write(src)
    print("f8trace: instrumented %d ROMC states in %s" % (n[0], path))
    return 0


def revert(path):
    bak = path + ".fujibak"
    if not os.path.exists(bak):
        sys.exit("f8trace: no backup at %s" % bak)
    open(path, "w").write(open(bak).read())
    os.remove(bak)
    print("f8trace: reverted %s" % path)
    return 0


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: f8trace.py <mame-tree> [--revert]")
    f8 = os.path.join(argv[1], "src/devices/cpu/f8/f8.cpp")
    if not os.path.exists(f8):
        sys.exit("f8trace: not a MAME tree: %s" % f8)
    return revert(f8) if "--revert" in argv else patch(f8)


sys.exit(main(sys.argv))
