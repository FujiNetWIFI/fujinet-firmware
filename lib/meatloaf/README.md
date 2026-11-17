# lib/meatloaf

The Meatloaf virtual filesystem (James Johnston / idolpx, GPL-3.0): every path or URL becomes an
`MFile`, and nested containers (an HTTP URL holding a D64 holding a PRG) resolve by chaining
filesystems. Locally modified.

## How it fits
- The IEC drive in [lib/device/iec/](../device/iec/) is the primary consumer. The WebDAV server in
  [lib/http/webdav/](../http/webdav/) and the console VFS commands in [lib/console/](../console/)
  use it too, which is why it links into every ESP build.
- Builds on `PeoplesUrlParser`, `mstr` and `U8Char` from [lib/utils/](../utils/) and on
  [lib/TNFSlib/](../TNFSlib/).

## Build
ESP: globbed into every target. PC: `FUJINET_TARGET=IEC` compiles it without the HTTP, TNFS, `ML:` and
CommodoreServer filesystems, which need ESP-IDF, lwIP or the ESP VFS. `FlashMFile::basepath` holds the
flash directory there; `iecFuji::setup()` sets it from `fsFlash`.

## Notes
- `file/prg.h` is a one-line placeholder, `service/gdrive.h` a stub, and `network/tcp.h` is not
  registered.
