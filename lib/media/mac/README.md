# lib/media/mac

Macintosh 68k floppy and hard-disk image formats for `BUILD_MAC`, plus the GCR encoder and StuffIt
mounting. ESP32 only.

## Layout
| File | Defines |
|---|---|
| `mediaType.h`, `mediaType.cpp` | this platform's `MediaType` base: block `read()` and `write()`, the track accessors `trackmap()`, `get_track()`, `track_len()` and `num_bits()` used for floppies, and `discover_mediatype()` |
| `mediaTypeMOOF.h`, `mediaTypeMOOF.cpp` | `MediaTypeMOOF`, flux-level floppy images |
| `mediaTypeDCD.h`, `mediaTypeDCD.cpp` | `MediaTypeDCD`, HD20 hard-disk images |
| `mediaTypeFloppyImage.h`, `mediaTypeFloppyImage.cpp` | `MediaTypeFloppyImage`, a 400K or 800K sector image (raw or DiskCopy 4.2) served as GCR tracks |
| `macGCR.h`, `macGCR.cpp` | the GCR track encoder and zone geometry (`mac_gcr_encode_sector()`, `mac_gcr_track_bits()` and friends) |
| `sitMount.h`, `sitMount.cpp` | `SitMount`, which unpacks a StuffIt, BinHex or MacBinary archive with [lib/stuffit/](../../stuffit/) into a PSRAM image and hands it to one of the media types |

## How it fits
- `discover_mediatype()` goes by the last extension: MOOF; IMAGE and DC42; DSK and IMG; SIT, SEA,
  HQX and BIN. Whether a DSK or IMG is a floppy or an HD20 is decided by the slot that `macFloppy`
  in [lib/device/mac/](../../device/mac/) mounts it in.
- Included by [lib/media/media.h](../media.h) under `BUILD_MAC`.

## Build
ESP: globbed by `src/CMakeLists.txt`; compiled only under `BUILD_MAC` except `macGCR.cpp`, which
has no guard and is built into every target. PC: not compiled, but `tests/mac_gcr_test.cpp` builds
`macGCR.cpp` on its own.

## Notes
`mediaType.cpp` ends in an `#if 0` block left from the Apple II media it was copied from.
