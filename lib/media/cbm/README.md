# lib/media/cbm

Commodore media type identifiers and blank disk images for `BUILD_IEC`, on ESP32 and FujiNet-PC.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base and `discover_mediatype()` (PRG, D64, D71, D81) |
| `BlankImage.h`, `BlankImage.cpp` | `BlankImage::create()`, the image NEW DISK writes: a formatted D64, D71 or D81, or zeros |
| `d64.h`, `d71.h`, `d80.h`, `d81.h`, `d82.h`, `d8b.h`, `t64.h`, `tcrt.h` | empty placeholder headers |

## How it fits
- No `MediaType` subclass exists here. The IEC drive in [lib/device/iec/](../../device/iec/) opens
  and decodes images through [lib/meatloaf/](../../meatloaf/), whose [disk/](../../meatloaf/disk/),
  [tape/](../../meatloaf/tape/) and [container/](../../meatloaf/container/) directories hold the
  real D64, T64 and TCRT code; this directory supplies the `mediatype_t` values and the blank
  images.
- `iecDrive::write_blank()` calls `BlankImage::create()` for NEW DISK. `discover_mediatype()` picks
  the format from the extension, in any case. A `.d64`, `.d71` or `.d81` gets that drive's
  standard-size image (174848, 349696 or 819200 bytes), formatted with every block free (664, 1328
  or 3160), an empty directory, the file name as the disk name and the ID `FN`; the size in the
  request is ignored. Any other name gets `sectorSize * numSectors` zero bytes; 0 and more than
  16 MiB are refused.
- Included by [lib/media/media.h](../media.h) under `BUILD_IEC`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`. PC: `FUJINET_TARGET=IEC` lists
the files in `fujinet_pc.cmake`; `tests/BlankImageTests.cpp` builds `BlankImage.cpp` and `mediaType.cpp`
on every target.
