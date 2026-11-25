#!/usr/bin/env bash
# run.sh <scenario> [--show] [--native] [--async] [--keep] [--trace] [--set KEY=VALUE]...
# run.sh boot <image>... [--native] [--async]
#
# Boots MAME's trs80fn (a Model I with the card, pico/trs80m1/emu) against fujinet-pc's
# BoIP port and drives emu/scenarios/<scenario>.steps with emu/m1text.lua. Header lines:
#   ; mount <slot> <image> [rw]    slot 0 is drive 0; client:NAME is build/client/NAME,
#                                  disk:NAME build/disks/NAME, libtest:NAME build/libtest/NAME
#   ; timeout <seconds>            wall clock for the whole run
#   ; switches <n>                 the card's RAM 1, EI 2, RS-232 4 (default 7)
#   ; http                         serve FNTEST.TXT at 127.0.0.1:8046, as FNTEST expects
#   ; echo                         a TCP echo server at 127.0.0.1:8047, for MDMTEST
#   ; printed TEXT                 fujinet-pc's printer output must hold TEXT, in any case
# Each run mounts fresh copies, so images stay as they were. --native boots stock
# trs80l2 with MAME's own FD1771 instead, as an oracle; --async lets emulated time run
# while the link waits; --trace writes every 37E0h-37EFh access to the run directory.
# --set KEY=VALUE puts VALUE for each %KEY% in the scenario. --show plays the scenario in a
# MAME window at real speed and leaves it open. `boot` opens a window with the images in
# slots 1, 2... (client:fnhello.dsk, client:autorun.jv1, disk:ldos531.dsk) to use by hand.
#
#   FNPC=~/Workspace/fnpc-trs80m1  FUJINET_TCP=127.0.0.1:1980  FNPC_WEB=127.0.0.1:8045
#   MAME=~/Workspace/mame-trs80  MAME_ARGS="-video soft" (more MAME options, for --show and boot)
set -euo pipefail
{ # parsed whole before it runs, so editing this file can't upset a run in progress
HERE=$(cd "$(dirname "$0")" && pwd)
FNPC=${FNPC:-$HOME/Workspace/fnpc-trs80m1}
FUJINET_TCP=${FUJINET_TCP:-127.0.0.1:1980}
FNPC_WEB=${FNPC_WEB:-127.0.0.1:8045}
MAME=${MAME:-$HOME/Workspace/mame-trs80}
ROMS=${ROMS:-$HOME/Workspace/mame/roms}
export FUJINET_TCP

NAME=${1:?usage: run.sh <scenario> [--show] [--native] [--async] [--keep] [--trace]}
shift
NATIVE=; KEEP=; TRACE=; SHOW=
SETS=()
BOOTS=()
if [ "$NAME" = boot ]; then
    SHOW=1
    while [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; do
        BOOTS+=("$1")
        shift
    done
fi
unset FUJINET_ASYNC
while [ $# -gt 0 ]; do
    case $1 in
    --native) NATIVE=1 ;;
    --async) export FUJINET_ASYNC=1 ;;
    --keep) KEEP=1 ;;
    --trace) TRACE=1 ;;
    --set) SETS+=("$2"); shift ;;
    --show) SHOW=1 ;;
    *) echo "run.sh: unknown option $1" >&2; exit 2 ;;
    esac
    shift
done
TAG=$NAME$(for pair in "${SETS[@]}"; do printf -- '-%s' "${pair#*=}"; done)
TAG=$TAG${NATIVE:+-native}${FUJINET_ASYNC:+-async}
OUT=$HERE/build/run/$TAG
rm -rf "$OUT"
mkdir -p "$OUT"
SCEN=$OUT/scenario
if [ "$NAME" = boot ]; then
    {
        echo "; ${BOOTS[*]}, by hand"
        echo "; timeout 0"
        for idx in "${!BOOTS[@]}"; do echo "; mount $idx ${BOOTS[$idx]} rw"; done
    } > "$SCEN"
else
    SRC_SCEN=$HERE/emu/scenarios/$NAME.steps
    [ -f "$SRC_SCEN" ] || { echo "run.sh: no $SRC_SCEN" >&2; exit 2; }
    cp "$SRC_SCEN" "$SCEN"
fi
for pair in "${SETS[@]}"; do
    sed -i "s|%${pair%%=*}%|${pair#*=}|g" "$SCEN"
done

resolve() {
    case $1 in
    client:*) echo "$HERE/build/client/${1#client:}" ;;
    disk:*) echo "$HERE/build/disks/${1#disk:}" ;;
    libtest:*) echo "$HERE/build/libtest/${1#libtest:}" ;;
    *) echo "$1" ;;
    esac
}

TIMEOUT=120
SWITCHES=7
HTTP=
ECHO=
PRINTED=
MOUNTS=()
while read -r semi key rest; do
    [ "$semi" = ";" ] || continue
    case $key in
    mount) MOUNTS+=("$rest") ;;
    timeout) TIMEOUT=$rest ;;
    switches) SWITCHES=$rest ;;
    http) HTTP=1 ;;
    echo) ECHO=1 ;;
    printed) PRINTED=$rest ;;
    esac
done < "$SCEN"

STARTED=
HOST=${FUJINET_TCP%:*}
PORT=${FUJINET_TCP##*:}
if [ -z "$NATIVE" ] && ! ss -ltn | grep -q "$HOST:$PORT "; then
    (cd "$FNPC" && exec ./fujinet -u "http://$FNPC_WEB" -c fnconfig.ini -s SD) \
        > "$OUT/fujinet.log" 2>&1 &
    STARTED=$!
    for _ in $(seq 50); do ss -ltn | grep -q "$HOST:$PORT " && break; sleep 0.2; done
fi
HTTPPID=
if [ -n "$HTTP" ]; then
    mkdir -p "$OUT/http"
    printf 'FUJINET MODEL I FNTEST OK\r\n' > "$OUT/http/FNTEST.TXT"
    (cd "$OUT/http" && exec python3 -I -m http.server 8046 --bind 127.0.0.1) \
        > "$OUT/http.log" 2>&1 &
    HTTPPID=$!
fi
ECHOPID=
if [ -n "$ECHO" ]; then
    python3 -I -c '
import socket, threading
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 8047)); srv.listen(4)
def serve(conn):
    while True:
        data = conn.recv(4096)
        if not data:
            break
        conn.sendall(data)
while True:
    conn, _ = srv.accept()
    threading.Thread(target=serve, args=(conn,), daemon=True).start()
' > "$OUT/echo.log" 2>&1 &
    ECHOPID=$!
fi
# Waits for each to go, so the next run finds its ports free.
cleanup() {
    [ -n "$STARTED" ] && [ -n "$KEEP" ] && STARTED=
    for pid in $HTTPPID $ECHOPID $STARTED; do
        kill "$pid" 2>/dev/null && wait "$pid" 2>/dev/null
    done
    true
}
trap cleanup EXIT

MARGS=(); FLOPS=()
mkdir -p "$FNPC/SD/run"
for spec in "${MOUNTS[@]}"; do
    read -r slot image mode <<< "$spec"
    src=$(resolve "$image")
    [ -f "$src" ] || { echo "run.sh: no image $src" >&2; exit 2; }
    copy=$TAG-$slot.${src##*.}
    if [ -n "$NATIVE" ]; then
        cp "$src" "$OUT/$copy"
        FLOPS+=(-flop$((slot + 1)) "$OUT/$copy")
    else
        cp "$src" "$FNPC/SD/run/$copy"
        MARGS+=("$slot=/run/$copy${mode:+:$mode}")
    fi
done
if [ -z "$NATIVE" ]; then
    python3 -I "$HERE/../common/tools/fnmount.py" --clear "${MARGS[@]}" > "$OUT/mount.log"
fi

STEPS=$OUT/steps
{
    if [ -n "$NATIVE" ]; then
        echo 'config :CONFIG "Floppy Disc Drives" 0x80'
    else
        echo "config :fujinet:SWITCHES \"FujiNet 32K RAM\" $((SWITCHES & 1))"
        echo "config :fujinet:SWITCHES \"FujiNet Expansion Interface\" $((SWITCHES & 2))"
        echo "config :fujinet:SWITCHES \"FujiNet RS-232\" $((SWITCHES & 4))"
    fi
    cat "$SCEN"
} > "$STEPS"

MACHINE=trs80fn
[ -n "$NATIVE" ] && MACHINE=trs80l2
if [ -n "$SHOW" ]; then
    # A window at real speed, left open: the steps play, then it's yours.
    M1_KEEP=${M1_KEEP:-1} M1_STEPS=$STEPS M1_TRACE=${TRACE:+$OUT/fdc.trace} \
        "$MAME/trs80" "$MACHINE" -rompath "$ROMS" -skip_gameinfo -window ${MAME_ARGS:-} \
        -autoboot_script "$HERE/emu/m1text.lua" -cfg_directory "$OUT/cfg" \
        -nvram_directory "$OUT/nvram" "${FLOPS[@]}" 2>&1 | tee "$OUT/mame.log" || true
    exit 0
fi
M1_STEPS=$STEPS M1_TRACE=${TRACE:+$OUT/fdc.trace} timeout -s KILL "$TIMEOUT" \
    "$MAME/trs80" "$MACHINE" -rompath "$ROMS" -skip_gameinfo -video none -sound none \
    -nothrottle -autoboot_script "$HERE/emu/m1text.lua" -cfg_directory "$OUT/cfg" \
    -nvram_directory "$OUT/nvram" "${FLOPS[@]}" > "$OUT/mame.log" 2>&1 || true

if [ -n "$PRINTED" ] && [ -z "$NATIVE" ]; then
    # fujinet-pc hands out /print only once the printer has been idle for 2 s.
    for _ in $(seq 10); do
        curl -s "http://$FNPC_WEB/print" > "$OUT/print.out" || true
        grep -aqiF "$PRINTED" "$OUT/print.out" && break
        sleep 1
    done
    if ! grep -aqiF "$PRINTED" "$OUT/print.out"; then
        echo "$TAG: FAIL (the printer never got \"$PRINTED\"; see $OUT/print.out)"
        exit 1
    fi
fi
if grep -q "^VERDICT PASS" "$OUT/mame.log"; then
    echo "$TAG: PASS"
    exit 0
fi
echo "$TAG: FAIL ($(grep '^VERDICT' "$OUT/mame.log" || echo 'no verdict'); see $OUT/mame.log)"
exit 1
}
