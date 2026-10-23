# lib/media/apple

Apple II disk image formats for `BUILD_APPLE`.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base: block `read()` and `write()`, `write_sector()`, `discover_mediatype()` and `discover_dsk_mediatype()` |
| `mediaTypePO.h`, `mediaTypePO.cpp` | `MediaTypePO`, ProDOS-order block images (also HDV) |
| `mediaType2MG.h`, `mediaType2MG.cpp` | `MediaType2MG : MediaTypePO`, a ProDOS-order image behind a 64-byte 2MG header, its high-score marker, and the header NEW DISK writes |
| `mediaTypeDO.h`, `mediaTypeDO.cpp` | `MediaTypeDO`, DOS-order images |
| `mediaTypeWOZ.h`, `mediaTypeWOZ.cpp` | `MediaTypeWOZ`, bit-level floppy images served to the Disk II emulation |
| `mediaTypeDSK.h`, `mediaTypeDSK.cpp` | `MediaTypeDSK : MediaTypeWOZ`, a sector DSK presented as WOZ tracks |

## How it fits
- `discover_mediatype()` by name maps HDV and PO to `MEDIATYPE_PO`, DO to `MEDIATYPE_DO`, and 2MG,
  WOZ and DSK to their own types; directory entries use it. At mount, the overload taking the file
  picks `MEDIATYPE_2MG` for any image that starts with `2IMG`, and otherwise the given type or the
  name's, with `discover_dsk_mediatype()` settling DSK. `iwmDisk` and `iwmDisk2` in
  [lib/device/iwm/](../../device/iwm/) choose the subclass.
- Included by [lib/media/media.h](../media.h) under `BUILD_APPLE`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_APPLE`. PC: `FUJINET_TARGET=APPLE`
lists every file; `tests/MediaType2MGTests.cpp` covers the PO and 2MG mounts and the 2MG header.
