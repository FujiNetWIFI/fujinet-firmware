# lib/media/mac

Macintosh 68k floppy and hard-disk image formats for `BUILD_MAC`, plus the GCR encoder and StuffIt
mounting. ESP32 only.

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
