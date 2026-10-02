# lib/media/drivewire

CoCo and Dragon disk, cartridge and cassette image formats for `BUILD_COCO`.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base: `MEDIA_BLOCK_SIZE` sector `read()` and `write()`, `get_block_buffer()`, `discover_mediatype()` |
| `mediaTypeDSK.h`, `mediaTypeDSK.cpp` | `MediaTypeDSK`, disk images |
| `mediaTypeVDK.h`, `mediaTypeVDK.cpp` | `MediaTypeVDK`, Dragon VDK images |
| `mediaTypeMRM.h`, `mediaTypeMRM.cpp` | `MediaTypeMRM` (MRM and RMM) |
| `mediaTypeROM.h`, `mediaTypeROM.cpp` | `MediaTypeROM`, cartridge images (ROM and CCC) |
| `mediaTypeCASDSK.h`, `mediaTypeCASDSK.cpp` | `MediaTypeCASDSK`, a `.cas` tape presented as a read-only Disk BASIC floppy synthesized from an index of the tape |
| `casSource.h`, `casSourceFile.h`, `casReader.h`, `casReader.cpp`, `casIndex.h`, `casIndex.cpp`, `decbLayout.h`, `decbLayout.cpp` | the tape decoder behind `MediaTypeCASDSK`: `CasSource`, `CasSourceFile`, `CasReader`, `CasIndex` and the Disk BASIC layout `DecbLayout` |

## How it fits
- `discover_mediatype()` maps DSK, MRM and RMM, VDK, ROM and CCC, and CAS by extension;
  `drivewireDisk` in [lib/device/drivewire/](../../device/drivewire/) picks the subclass.
- Included by [lib/media/media.h](../media.h) under `BUILD_COCO`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`. PC: `FUJINET_TARGET=COCO`
lists every file.
