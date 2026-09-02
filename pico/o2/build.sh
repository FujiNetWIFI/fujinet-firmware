#!/bin/bash
# Assemble the stub BIOS and a cart image with Macroassembler AS (run under wine
# from ~/Workspace/o2workspace/bin until AS is built natively).
set -e
AS_BIN="${AS_BIN:-$HOME/Workspace/o2workspace/bin}"
OUT="${OUT:-$PWD/build}"
mkdir -p "$OUT"
run_as() { ( cd "$OUT" && WINEDEBUG=-all wine "$AS_BIN/$@" ); }

# stub BIOS: $000-$3FF
cp emu/stubbios.a48 "$OUT/"
run_as asw.exe -q -L stubbios.a48
run_as p2bin.exe stubbios.p stubbios.bin -r 0x0-0x3FF -l 0

# cart: $400-$FFF (3K)
cp testrom/*.a48 testrom/*.inc testrom/g7000.h "$OUT/"
for src in "$@"; do
  b=$(basename "$src" .a48)
  run_as asw.exe -q -L "$b.a48"
  run_as p2bin.exe "$b.p" "$b.bin" -r 0x400-0xFFF -l 0
done
ls -l "$OUT"/*.bin
