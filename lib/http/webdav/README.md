# lib/http/webdav

The WebDAV server the ESP32 web UI exposes under the dav path, giving desktop file managers access
to the SD card.

## Layout
| File | Defines |
|---|---|
| `webdav_server.*` | `WebDav::Server`: the method handlers over a root URI and root path |
| `request.*`, `response.*` | `WebDav` request and response wrappers around `httpd_req_t` |
| `handler.*` | `webdav_register()`: binds the server into `esp_http_server`; requires a web session or Basic auth when a device password is set |
| `file-utils.h` | Path and file helpers |

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
