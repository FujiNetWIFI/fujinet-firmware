# lib/media/h89

Heathkit H89 disk images for `BUILD_H89`. Legacy (#1658).

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base and a `supported_images` table of RomWBW and CP/M geometries; `discover_mediatype()` matches extension (IMG, CPM, DSK) and file size |
| `mediaTypeIMG.h`, `mediaTypeIMG.cpp` | `MediaTypeIMG`, the single image class |

## How it fits
- Included by [lib/media/media.h](../media.h) under `BUILD_H89`; consumed by
  [lib/device/h89/](../../device/h89/). [lib/media/rc2014/](../rc2014/) is a near copy of this
  directory.

## Build
ESP only, `BUILD_H89`; not compiled on PC.
