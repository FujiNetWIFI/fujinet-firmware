# lib/media/apple

Apple II disk image formats for `BUILD_APPLE`.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base: block `read()` and `write()`, `write_sector()`, `discover_mediatype()` and `discover_dsk_mediatype()` |
| `mediaTypePO.h`, `mediaTypePO.cpp` | `MediaTypePO`, ProDOS-order block images (also HDV and 2MG) |
| `mediaTypeDO.h`, `mediaTypeDO.cpp` | `MediaTypeDO`, DOS-order images |
| `mediaTypeWOZ.h`, `mediaTypeWOZ.cpp` | `MediaTypeWOZ`, bit-level floppy images served to the Disk II emulation |
| `mediaTypeDSK.h`, `mediaTypeDSK.cpp` | `MediaTypeDSK : MediaTypeWOZ`, a sector DSK presented as WOZ tracks |

## How it fits
- `discover_mediatype()` maps HDV, 2MG and PO to `MEDIATYPE_PO`, DO to `MEDIATYPE_DO`, and WOZ and
  DSK to their own types; `iwmDisk` and `iwmDisk2` in [lib/device/iwm/](../../device/iwm/) choose
  the subclass.
- Included by [lib/media/media.h](../media.h) under `BUILD_APPLE`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_APPLE`. PC: `FUJINET_TARGET=APPLE`
lists every file.
