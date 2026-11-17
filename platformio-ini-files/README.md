# platformio-ini-files

The shared base layer of every generated PlatformIO configuration.

## How it fits
- `create-platformio-ini.py` merges, in order, `platformio.common.ini`, then the board file
  `build-platforms/platformio-<build_board>.ini`, then `platformio.local.ini`, into
  `platformio-generated.ini`. In the local ini a key written as `key +=` appends to the inherited
  value and a plain `key =` replaces it.
- `build_board` in the local ini's `[fujinet]` section selects the board file; see
  [build-platforms/](../build-platforms/) and `build-sh.md`.

## Build
Read on every `build.sh` run for an ESP32 target. The PC build does not use it.

## Notes
Do not edit `platformio.common.ini`; local settings belong in the git-ignored
`platformio.local.ini`. A board ini must not redefine `extra_scripts`, because a plain key replaces
the whole list and silently drops `build_pico.py`. `lib_ldf_mode` is off, so PlatformIO never
scans `lib/` for dependencies; `src/CMakeLists.txt` lists every source directory explicitly.
The comment block in `platformio.common.ini` is the reference for every `[fujinet]` `pico_*` and
`merge_bin` key. Only `build.sh -z` merges `platformio.zip-options.ini`.
