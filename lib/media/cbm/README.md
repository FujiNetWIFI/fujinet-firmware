# lib/media/cbm

Commodore media type identifiers for `BUILD_IEC`. ESP32 only.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base and `discover_mediatype()` (PRG, D64, D71, D81) |
| `d64.h`, `d71.h`, `d80.h`, `d81.h`, `d82.h`, `d8b.h`, `t64.h`, `tcrt.h` | empty placeholder headers |

## How it fits
- No `MediaType` subclass exists here. The IEC drive in [lib/device/iec/](../../device/iec/) opens
  and decodes images through [lib/meatloaf/](../../meatloaf/), whose [disk/](../../meatloaf/disk/),
  [tape/](../../meatloaf/tape/) and [container/](../../meatloaf/container/) directories hold the
  real D64, T64 and TCRT code; this directory only supplies the `mediatype_t` values.
- Included by [lib/media/media.h](../media.h) under `BUILD_IEC`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_IEC`. Not compiled on PC.
