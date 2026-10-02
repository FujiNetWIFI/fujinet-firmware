# lib/media/cx16

Commander X16 media placeholder for `BUILD_CX16`. Legacy (#1658).

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base; `discover_mediatype()` always returns `MEDIATYPE_UNKNOWN` and no subclass exists |

## How it fits
- Included by [lib/media/media.h](../media.h) under `BUILD_CX16`; consumed by
  [lib/device/cx16_i2c/](../../device/cx16_i2c/).

## Build
ESP only, `BUILD_CX16`; not compiled on PC.
