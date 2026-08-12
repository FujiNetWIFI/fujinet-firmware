#!/bin/bash

rm -rf build

boards=("pintycard" "pirto_ii_default" "pirto_ii_duo" "pirto_ii_sd" "pirto")

for board in "${boards[@]}"; do

   cmake -B build/$board/debug -DPICO_BOARD=$board -DCMAKE_BUILD_TYPE=Debug
   make -j -C build/$board/debug

   cmake -B build/$board/release -DPICO_BOARD=$board -DCMAKE_BUILD_TYPE=Release
   make -j -C build/$board/release

done


