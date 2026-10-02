# lib/media/s100spi

S-100 disk images for `BUILD_S100`. Legacy and unreachable (#1658).

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base; `discover_mediatype()` maps DSK only |
| `mediaTypeDSK.h`, `mediaTypeDSK.cpp` | `MediaTypeDSK` |

## How it fits
- [lib/media/media.h](../media.h) maps `BUILD_S100` to [lib/media/adam/](../adam/) instead of this
  directory, so nothing includes these files.

## Build
ESP: globbed by `src/CMakeLists.txt` under `BUILD_S100`, but never included. Not compiled on PC.
