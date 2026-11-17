# lib/media/s100spi

S-100 disk images for `BUILD_S100`. Legacy and unreachable (#1658).

## How it fits
- [lib/media/media.h](../media.h) maps `BUILD_S100` to [lib/media/adam/](../adam/) instead of this
  directory, so nothing includes these files.

## Build
ESP: globbed by `src/CMakeLists.txt` under `BUILD_S100`, but never included. Not compiled on PC.
