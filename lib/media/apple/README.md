# lib/media/apple

Apple II disk image formats for `BUILD_APPLE`.

## How it fits
- `discover_mediatype()` by name maps HDV and PO to `MEDIATYPE_PO`, DO to `MEDIATYPE_DO`, and 2MG,
  WOZ and DSK to their own types; directory entries use it. At mount, the overload taking the file
  picks `MEDIATYPE_2MG` for any image that starts with `2IMG`, and otherwise the given type or the
  name's, with `discover_dsk_mediatype()` settling DSK. `iwmDisk` and `iwmDisk2` in
  [lib/device/iwm/](../../device/iwm/) choose the subclass.
- Included by [lib/media/media.h](../media.h) under `BUILD_APPLE`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_APPLE`. PC: `FUJINET_TARGET=APPLE`
lists every file; `tests/MediaType2MGTests.cpp` covers the PO and 2MG mounts and the 2MG header.
