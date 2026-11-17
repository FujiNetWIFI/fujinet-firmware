# lib/http/webdav

The WebDAV server the ESP32 web UI exposes under the dav path, giving desktop file managers access
to the SD card.

## How it fits
- Registered by [httpService.cpp](../httpService.cpp) in [lib/http/](../) with the SD card as its
  root path.
- File access goes through `MFSOwner::File` and `FlashMFile` from [lib/meatloaf/](../../meatloaf/),
  which is why Meatloaf is linked into every ESP build.
- Auth uses `fnPassword` from [lib/config/](../../config/) and `fnSession` from [lib/http/](../).
- Unrelated to [lib/webdav/](../../webdav/), which holds client-side listing parsers.

## Build
ESP only: globbed by `src/CMakeLists.txt` (the `lib/http` glob is recursive) into every target;
`handler.cpp` compiles to nothing under `MIN_CONFIG`. Not listed in `fujinet_pc.cmake`.

## Notes
Ported from the Meatloaf project (GPL-3, Copyright 2020 James Johnston).

- `webdav_methods.h` is host-tested by `tests/WebDavMethodsTests.cpp`.
