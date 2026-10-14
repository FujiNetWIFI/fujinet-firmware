#!/usr/bin/env bash
# apply.sh <simh> -- graft the FujiNet S-100 card into a SIMH tree's AltairZ80.
# Rerunnable: copies the card core and the device, then patches the makefile,
# the device list and a running T-state clock into the CPU, once each.
set -euo pipefail
SIMH=$(cd "${1:?usage: apply.sh <simh tree>}" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
FW=$HERE/../firmware
AZ=$SIMH/AltairZ80

mkdir -p "$AZ/fujis100"
cp "$FW"/src/s100_{regs,fdc,2sio,link}.c "$FW"/include/s100_*.h "$AZ/fujis100/"
cp "$HERE/../../common/src/fujibus.c" "$HERE/../../common/include/fujibus.h" "$AZ/fujis100/"
# The boot page and images: build.sh's fresh ones, else the baked ones.
for h in s100_bootpage.h s100_images.h; do
    if [ -f "$HERE/../build/gen/$h" ]; then
        cp "$HERE/../build/gen/$h" "$AZ/fujis100/"
    elif [ -f "$FW/baked/$h" ]; then
        cp "$FW/baked/$h" "$AZ/fujis100/"
    fi
done
cp "$HERE/s100_fujinet.c" "$AZ/"

python3 -I - "$SIMH" <<'PY'
import sys, re
simh = sys.argv[1]

# SIMH's sources are CRLF: match on LF, write back what was there.
def edit(path, marker, fn):
    raw = open(path, newline="").read()
    crlf = "\r\n" in raw
    s = raw.replace("\r\n", "\n")
    if marker in s:
        return
    t = fn(s)
    if t == s or marker not in t:
        sys.exit("apply.sh: could not patch " + path)
    open(path, "w", newline="").write(t.replace("\n", "\r\n") if crlf else t)

core = ("${ALTAIRZ80D}/s100_fujinet.c ${ALTAIRZ80D}/fujis100/s100_regs.c "
        "${ALTAIRZ80D}/fujis100/s100_fdc.c ${ALTAIRZ80D}/fujis100/s100_2sio.c "
        "${ALTAIRZ80D}/fujis100/s100_link.c ${ALTAIRZ80D}/fujis100/fujibus.c \\\n\t")
edit(simh + "/makefile", "s100_fujinet.c",
     lambda s: s.replace("ALTAIRZ80 = ${ALTAIRZ80D}/altairz80_cpu.c",
                         "ALTAIRZ80 = " + core + "${ALTAIRZ80D}/altairz80_cpu.c", 1)
                .replace("ALTAIRZ80_OPT = -I ${ALTAIRZ80D}",
                         "ALTAIRZ80_OPT = -I ${ALTAIRZ80D} -I ${ALTAIRZ80D}/fujis100", 1))

sysc = simh + "/AltairZ80/altairz80_sys.c"
edit(sysc, "fujinet_dev",
     lambda s: s.replace("extern DEVICE dsk_dev;", "extern DEVICE dsk_dev;\nextern DEVICE fujinet_dev;", 1)
                .replace("&mhdsk_dev, &hdsk_dev, &net_dev,", "&mhdsk_dev, &hdsk_dev, &net_dev, &fujinet_dev,", 1))

cpu = simh + "/AltairZ80/altairz80_cpu.c"
def cpu_patch(s):
    s = s.replace("    register uint32 tStates;    /* number of t-states executed in the current time-slice */\n", "", 1)
    s = s.replace("uint32 getClockFrequency(void) {",
                  "/* FujiNet: t-states since start-up, so devices can tell time */\n"
                  "static uint32 tStates;\nstatic t_uint64 fujinet_tstate_base;\n"
                  "t_uint64 fujinet_tstates(void) {\n    return fujinet_tstate_base + tStates;\n}\n\n"
                  "uint32 getClockFrequency(void) {", 1)
    s = s.replace("    tStates = 0;\n    if (rtc_avail) {",
                  "    fujinet_tstate_base += tStates;\n    tStates = 0;\n    if (rtc_avail) {", 1)
    s = s.replace("                tStates = 0;\n                if (rtc_avail) {",
                  "                fujinet_tstate_base += tStates;\n                tStates = 0;\n                if (rtc_avail) {", 1)
    s = s.replace("                tStates -= tStatesInSlice;  /* reduce by t-states executed in a slice */",
                  "                fujinet_tstate_base += tStatesInSlice;\n"
                  "                tStates -= tStatesInSlice;  /* reduce by t-states executed in a slice */", 1)
    return s
edit(cpu, "fujinet_tstates", cpu_patch)
PY
echo "apply.sh: grafted into $SIMH; build with: make -C $SIMH altairz80"
