# lib/media/rc2014

RC2014 disk images for `BUILD_RC2014`. Legacy (#1658).

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base and a `supported_images` table of RomWBW and CP/M geometries; `discover_mediatype()` matches extension (IMG, CPM, DSK) and file size |
| `mediaTypeIMG.h`, `mediaTypeIMG.cpp` | `MediaTypeIMG`, the single image class |

## How it fits
- Included by [lib/media/media.h](../media.h) under `BUILD_RC2014`; consumed by
  [lib/device/rc2014/](../../device/rc2014/). A near copy of [lib/media/h89/](../h89/).

## Build
ESP only, `BUILD_RC2014`; not compiled on PC.
