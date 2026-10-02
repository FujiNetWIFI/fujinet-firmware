# lib/meatloaf

The Meatloaf virtual filesystem (James Johnston / idolpx, GPL-3.0): every path or URL becomes an
`MFile`, and nested containers (an HTTP URL holding a D64 holding a PRG) resolve by chaining
filesystems. Locally modified. ESP only.

## Layout
| File | Defines |
|---|---|
| `meatloaf.h`, `meatloaf.cpp` | `MStream`, `MFile` (derived from `PeoplesUrlParser`), `MFileSystem`, and `MFSOwner`, the registry whose `availableFS` list is searched first match wins |
| `meat_media.h`, `meat_media.cpp`; `meat_buffer.h` | `MMediaStream`, the base for sector images; stream buffers |
| `device/` | flash (the default filesystem) and SD |
| `disk/` | D64, with D71, D80, D81, D82, D90 and DNP as header-only variants |
| `container/` | D8B and DFI containers |
| `file/` | P00; `file/prg.h` is a one-line placeholder |
| `network/` | HTTP and TNFS; `network/tcp.h` is not registered |
| `service/` | CommodoreServer (`csip`) and the `ML:` catalogue; `service/gdrive.h` is a stub |
| `tape/` | T64 and TCRT |
| `wrappers/` | `iec_buffer` and `directory_stream`, the IEC-bus stream adapters |

## How it fits
- The IEC drive in [lib/device/iec/](../device/iec/) is the primary consumer. The WebDAV server in
  [lib/http/webdav/](../http/webdav/) and the console VFS commands in [lib/console/](../console/)
  use it too, which is why it links into every ESP build.
- Builds on `PeoplesUrlParser`, `mstr` and `U8Char` from [lib/utils/](../utils/) and on
  [lib/TNFSlib/](../TNFSlib/).

## Build
ESP: globbed into every target (`meatloaf.cpp` includes the ESP-IDF flash filesystem headers). PC:
not compiled.
