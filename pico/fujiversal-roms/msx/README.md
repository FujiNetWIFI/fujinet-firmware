# MSX CONFIG ROM (prebuilt)

`config-msx.rom` is the MSX-side FujiNet CONFIG program that the `fujiversal-msx` pico
firmware serves to the MSX as its cartridge ROM. It is checked in **prebuilt** rather than
built during the ESP32 build, because building it needs a Z80 toolchain (z88dk) plus
fujinet-lib, neither of which the firmware or CI toolchain carries.

It lives here, outside the `pico/fujiversal` submodule, so the submodule stays a clean
checkout of upstream. The CoCo equivalent (`hdbdw3bc3.rom`) needs no such treatment — it is
tracked inside the submodule already.

`build_pico.py` turns this file into `pico/fujiversal/build/msx_proto_260402/rom.h` via
`pico/tools/rom2h.py` before each pico build; see the `pico_prebuild` key in
`build-platforms/platformio-fujiversal-msx.ini`.

## Provenance

| | |
|---|---|
| sha256 | `34aec0c577da9e1bedc25f73367170bcbe4c209b4fb08f4a485e84ab284d4f6c` |
| size | 32768 bytes |
| source | `msxio/` in the `pico/fujiversal` submodule |
| built from submodule commit | `2f8950a` ("Updates to serial debugging (#19)") |
| fujinet-lib version | unrecorded — latest at build time (see the note below) |

The submodule is currently pinned at `929e2f8`, which is three commits newer than the commit
this ROM was built from. Those three commits touch `Makefile` and
`boards/coco_proto_260402.pio` only, not `msxio/`, so the ROM is still current. Check
`git -C pico/fujiversal log 2f8950a..HEAD -- msxio` before assuming it stays that way.

## Regenerating

From the repository root:

```sh
git -C pico/fujiversal log -1 --format=%H          # record this in the table above
make -C pico/fujiversal config-msx.rom             # needs z88dk + fujinet-lib
cp pico/fujiversal/config-msx.rom pico/fujiversal-roms/msx/config-msx.rom
sha256sum pico/fujiversal-roms/msx/config-msx.rom  # update the table above
```

The submodule's `Makefile` wraps every build command in `defoogi`, a Docker wrapper
(`fozztexx/defoogi:latest`) that carries the Z80 toolchain. Without Docker, run the
`msxio` build directly with a local z88dk instead; `pico/fujiversal/msxio/Makefile` takes
`FUJINET_LIB=` as a version number, directory, zip, or git URL.

Commit the regenerated ROM together with the updated table above, and say in the commit
message which submodule commit and fujinet-lib version produced it.
