#!/usr/bin/env bash
# run.sh <scenario> [--cpu 8080|z80] [--mhz 2|4] [--sync|--async] [--native] [--keep]
#        [--set KEY=VALUE]...
#
# Boots SIMH AltairZ80 with the FujiNet card (pico/s100/emu) against fujinet-pc's
# BoIP port and runs emu/scenarios/<scenario>.sim. Its header lines pick the disks:
#   ; mount <slot> <image> [rw]      ; timeout <seconds>
#   ; rom dbl|mdbl|none    ; http    ; echo    ; printed TEXT    ; mode async
#   ; must TEXT     (the transcript has to contain it)
#   ; image0 NAME   (build/client/NAME as the card's image 0)
# (`none` boots from the card's own page at FF00 instead of a MITS ROM.)
# Images come from $ALTAIR_DISKS (z80pack's library) or build/disks; each run
# mounts fresh copies. PASS needs the scenario's "*** PASS ***" on the console.
# --native runs the same scenario on SIMH's own 88-DCDD instead, as an oracle;
# the final images land in the run directory either way.
#
#   SIMH=~/Workspace/simh-s100  FNPC=~/Workspace/fnpc-s100  FUJINET_TCP=127.0.0.1:1995
#   FUJI_IMAGE0=CONFIG.BIN FUJI_IMAGE1=loader.bin override the images the graft baked.
#   FNPC_WEB=http://127.0.0.1:8040 is fujinet-pc's web UI, for `; printed`.
set -euo pipefail
{ # parsed whole before it runs, so editing this file can't upset a run in progress
HERE=$(cd "$(dirname "$0")" && pwd)
SIMH=${SIMH:-$HOME/Workspace/simh-s100}
FNPC=${FNPC:-$HOME/Workspace/fnpc-s100}
FUJINET_TCP=${FUJINET_TCP:-127.0.0.1:1995}
LIB=${ALTAIR_DISKS:-$HOME/Workspace/z80pack/altairsim/disks/library}
DBLHEX=${DBLHEX:-$HOME/Workspace/z80pack/altairsim/roms/dbl.hex}
FNLIB=${FNLIB:-$HOME/Workspace/fnlib-s100}
FNCFG=${FNCFG:-$HOME/Workspace/fncfg-s100}
export FUJINET_TCP

scen=${1:?usage: run.sh <scenario> [--cpu 8080|z80] [--mhz 2|4] [--sync|--async] [--native] [--keep]}
shift
cpu=8080 mhz=2 mode=sync native=0 keep=0 sets=()
while [ $# -gt 0 ]; do
    case $1 in
    --set) sets+=("$2"); shift ;;
    --cpu) cpu=$2; shift ;;
    --mhz) mhz=$2; shift ;;
    --sync) mode=sync ;;
    --async) mode=async ;;
    --native) native=1 ;;
    --keep) keep=1 ;;
    *) echo "run.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

SIM=$HERE/emu/scenarios/$scen.sim
[ -f "$SIM" ] || { echo "run.sh: no scenario $SIM" >&2; exit 2; }
# Unsolicited traffic (the modem) needs real time: SYNC would let the CPU's
# own timeouts run out before the network answers.
grep -q '^; mode async' "$SIM" && mode=async
tag=$scen$(for kv in "${sets[@]}"; do printf -- '-%s' "${kv#*=}"; done)-$cpu-${mhz}mhz-$([ $native = 1 ] && echo native || echo $mode)
OUT=$HERE/build/run/$tag
rm -rf "$OUT"
mkdir -p "$OUT"

# Boot ROMs: the MITS DBL 4.1 from z80pack, or MDBL for minidisks from
# tools/fetch-disks.sh (`; rom mdbl`). Never committed.
rom=$(sed -n 's/^; rom \([a-z]*\).*/\1/p' "$SIM" | head -1)
rom=${rom:-dbl}
case $rom in
dbl) romhex=$DBLHEX ;;
mdbl) romhex=$HERE/build/disks/mdbl.hex ;;
none) romhex= ;; # the card's own boot page at FF00
*) echo "run.sh: unknown rom $rom" >&2; exit 2 ;;
esac
DBL=$HERE/build/run/$rom.bin
[ -z "$romhex" ] || [ -f "$DBL" ] || objcopy -I ihex -O binary "$romhex" "$DBL"
page=$([ -z "$romhex" ] && echo ff || echo fe)

find_disk() {
    for d in "$LIB" "$HERE/build/disks"; do
        [ -f "$d/$1" ] && { echo "$d/$1"; return; }
    done
    echo "run.sh: no image $1" >&2
    exit 2
}

timeout=$(sed -n 's/^; timeout \([0-9]*\).*/\1/p' "$SIM" | head -1)
timeout=${timeout:-120}
mounts=$(sed -n 's/^; mount //p' "$SIM")

host=${FUJINET_TCP%:*} port=${FUJINET_TCP##*:}
# BoIP takes one client, so look for the listener rather than connecting to it.
listening() { [ -n "$(ss -ltnH "sport = :$port")" ]; }

# A run of this same tag left behind by an interrupted harness still holds
# the one BoIP client slot.
pkill -9 -f "^$SIMH/BIN/altairz80 $OUT/run.ini" 2>/dev/null || true

INI=$OUT/run.ini
{
    echo "set console log=$OUT/console.log"
    echo "set cpu $cpu"
    echo "set cpu 64k"
    echo "set cpu noaltairrom"
} > "$INI"

if [ $native = 1 ]; then
    while read -r slot img rw; do
        [ -n "$slot" ] || continue
        cp "$(find_disk "$img")" "$OUT/slot$slot.dsk"
        echo "attach dsk$slot $OUT/slot$slot.dsk" >> "$INI"
    done <<< "$mounts"
    echo "set fujinet disabled" >> "$INI"
else
    if ! listening; then
        (cd "$FNPC" && nohup ./run-fujinet -u http://0.0.0.0:8040 -c fnconfig.ini -s SD \
            > fujinet.log 2>&1 &)
        for _ in $(seq 50); do listening && break; sleep 0.2; done
    fi
    mkdir -p "$FNPC/SD/run"
    margs=()
    while read -r slot img rw; do
        [ -n "$slot" ] || continue
        cp "$(find_disk "$img")" "$FNPC/SD/run/slot$slot.dsk"
        margs+=("$slot=/run/slot$slot.dsk${rw:+:rw}")
    done <<< "$mounts"
    python3 -I "$HERE/tools/fnmount.py" --clear "${margs[@]}" > "$OUT/mount.log"
    {
        echo "set dsk disabled"
        echo "set fujinet enabled"
        echo "set fujinet page=$page"
        echo "set fujinet $mode"
        echo "set fujinet mhz=$mhz"
        img0=$(sed -n 's/^; image0 //p' "$SIM" | head -1)
        [ -z "$img0" ] || FUJI_IMAGE0=${FUJI_IMAGE0:-$HERE/build/client/$img0}
        [ -z "${FUJI_IMAGE0:-}" ] || echo "set fujinet image0=$FUJI_IMAGE0"
        [ -z "${FUJI_IMAGE1:-}" ] || echo "set fujinet image1=$FUJI_IMAGE1"
        echo "attach fujinet $FUJINET_TCP"
    } >> "$INI"
fi
[ $mode = async ] && [ $native = 0 ] && echo "deposit cpu clock $((mhz * 1000))" >> "$INI"
# `; http` serves build/run/<run>/http with the file FNTEST expects.
httppid=
if grep -q '^; http' "$SIM"; then
    mkdir -p "$OUT/http"
    printf 'FUJINET S-100 FNTEST OK\r\n' > "$OUT/http/FNTEST.TXT"
    hport=$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    (cd "$OUT/http" && exec python3 -I -m http.server "$hport" --bind 127.0.0.1) > "$OUT/http.log" 2>&1 &
    httppid=$!
fi

# `; echo` runs a TCP echo server for the modem test.
echopid=
if grep -q '^; echo' "$SIM"; then
    eport=$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    python3 -I -c '
import socket, sys, threading
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", int(sys.argv[1]))); srv.listen(4)
def serve(c):
    while True:
        b = c.recv(4096)
        if not b:
            break
        c.sendall(b)
while True:
    c, _ = srv.accept()
    threading.Thread(target=serve, args=(c,), daemon=True).start()
' "$eport" > "$OUT/echo.log" 2>&1 &
    echopid=$!
fi

# Tokens: %CLIENT% is build/client, %FNLIBTEST% fujinet-lib's built test
# programs, %CONFIGCOM% fujinet-config's CONFIG.COM, %PAGES:DIR/NAME.COM% that
# file's SAVE count, %HTTP% the host:port above, and %KEY% for each --set
# KEY=VALUE.
{
    echo "reset"
    [ -z "$romhex" ] || echo "load $DBL ff00"
    grep -v '^;' "$SIM"
} | python3 -I -c '
import math, os, re, sys
toks = dict(a.split("=", 1) for a in sys.argv[1:])
for line in sys.stdin:
    for k, v in toks.items():
        line = line.replace("%" + k + "%", v)
    line = re.sub(r"%PAGES:([^%]+)%", lambda m: str(math.ceil(os.path.getsize(m.group(1)) / 256)), line)
    sys.stdout.write(line)
' "CLIENT=$HERE/build/client" "FNLIBTEST=$FNLIB/testing/r2r/s100-cpm" \
  "CONFIGCOM=$FNCFG/r2r/s100-cpm/CONFIG.COM" \
  "HTTP=127.0.0.1:${hport:-0}" "ECHO=127.0.0.1:${eport:-0}" "${sets[@]}" >> "$INI"

# SIMH ignores SIGTERM: run it in its own session and kill that whole session.
# Its console log is the transcript; no pty needed.
setsid "$SIMH/BIN/altairz80" "$INI" > "$OUT/tty.out" 2>&1 < /dev/null &
pid=$!
end=$((SECONDS + timeout))
while kill -0 $pid 2>/dev/null && [ $SECONDS -lt $end ]; do
    grep -qa '\*\*\* PASS \*\*\*' "$OUT/console.log" 2>/dev/null && sleep 0.5 && break
    sleep 0.2
done
kill -9 -- -$pid 2>/dev/null || true
wait $pid 2>/dev/null || true
[ -n "$httppid" ] && kill "$httppid" 2>/dev/null || true
[ -n "$echopid" ] && kill "$echopid" 2>/dev/null || true

# `; printed TEXT`: the run passes only if fujinet-pc's printer output has it.
printed=$(sed -n 's/^; printed //p' "$SIM" | head -1)
if [ -n "$printed" ] && [ $native = 0 ]; then
    # fujinet-pc refuses /print until the printer has been idle for 2 s.
    for _ in $(seq 10); do
        curl -s "${FNPC_WEB:-http://127.0.0.1:8040}/print" > "$OUT/print.out" || true
        grep -qa "$printed" "$OUT/print.out" && break
        sleep 1
    done
    if ! grep -qa "$printed" "$OUT/print.out"; then
        echo "$tag: FAIL (printer output lacks \"$printed\", see $OUT/print.out)"
        exit 1
    fi
fi

if [ $native = 0 ]; then
    while read -r slot img rw; do
        [ -n "$slot" ] || continue
        cp "$FNPC/SD/run/slot$slot.dsk" "$OUT/slot$slot.dsk"
    done <<< "$mounts"
fi

# `; must TEXT`: SIMH expect rules fire independently, so a check that never
# matched doesn't stop the PASS rule; these do.
while IFS= read -r want; do
    [ -n "$want" ] || continue
    if ! cat "$OUT/console.log" "$OUT/tty.out" 2>/dev/null | tr -d '\r' | grep -qaF -- "$want"; then
        echo "$tag: FAIL (never saw \"$want\", see $OUT/console.log)"
        exit 1
    fi
done <<< "$(sed -n 's/^; must //p' "$SIM")"

if cat "$OUT/console.log" "$OUT/tty.out" 2>/dev/null | grep -qa '\*\*\* PASS \*\*\*'; then
    echo "$tag: PASS"
    [ $keep = 1 ] || rm -f "$OUT/tty.out"
    exit 0
fi
echo "$tag: FAIL (see $OUT/console.log)"
tail -n 15 "$OUT/console.log" 2>/dev/null | tr -d '\r' | sed 's/^/    /'
exit 1
exit
}
