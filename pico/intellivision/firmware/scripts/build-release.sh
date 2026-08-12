#!/bin/bash

rm -rf build

boards=("pintycard" "pirto_ii_default" "pirto_ii_duo" "pirto_ii_sd" "pirto")

for board in "${boards[@]}"; do

   cmake -B build/$board/release -DPICO_BOARD=$board -DCMAKE_BUILD_TYPE=Release
   make -j -C build/$board/release

done

##

boards_ecs_iv=("pintycard" "pirto_ii_duo")

for board in "${boards_ecs_iv[@]}"; do

   cmake -B build/$board/release-ecs-iv -DPICO_BOARD=$board -DCMAKE_BUILD_TYPE=Release \
      -DCONFIG_ECS_AUDIO=ON -DCONFIG_INTELLIVOICE=ON -DOUTPUT_SUFFIX=_ecs_iv
   make -j -C build/$board/release-ecs-iv

done


