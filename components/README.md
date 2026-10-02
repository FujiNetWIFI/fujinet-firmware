# components

Vendored ESP-IDF components. Each directory is a copy of an upstream library with a FujiNet
`CMakeLists.txt` that registers it as an IDF component; ESP-IDF builds every directory here into
every ESP32 target.

## Layout
| Component | Upstream and version | Licence | Consumer | Status |
|---|---|---|---|---|
| `esp_littlefs` | joltwallet esp_littlefs 1.14.8 | MIT | `lib/FileSystem/fnFsLittleFS.cpp`, `lib/meatloaf` | used |
| `expat` | Espressif expat component 2.5.0 | MIT | `lib/network-protocol/HTTP.h`, `lib/webdav/WebDAV.h` | used |
| `gumbo` | Gumbo HTML5 parser, the Codeberg fork of google/gumbo-parser | Apache-2.0 | backend of `gumbo-query` | used |
| `gumbo-query` | CSS selectors over Gumbo, patched to build without exceptions and to allocate in PSRAM | MIT | `lib/fnhtml` | used |
| `libnfs` | libnfs with an `esp32` compatibility layer | LGPL-2.1 | `lib/FileSystem/fnFsNFS.cpp` | used |
| `libsmb2` | libsmb2 3.0.1 | LGPL-2.1 | `lib/FileSystem/fnFsSMB.cpp` | used |
| `libssh` | libssh 0.11.3, pared down to the mbedTLS backend with local `libssh/stubs.h` and `libssh/config.h` | LGPL-2.1 | `lib/network-protocol/SSH.h`, `lib/network-protocol/SSHCopyId.cpp` | used |
| `lz4` | LZ4 1.10.0 | BSD-2-Clause | `lib/device/comlynx/disk.cpp` | used |
| `mdns` | Espressif mDNS 1.8.2 | Apache-2.0 | `lib/hardware/fnWiFi.cpp` | used |
| `mlff` | Meatloaf File Flasher, idolpx | GPL-3.0 | `lib/hardware/fnSystem.cpp` | used; in-house, not a copy |
| `zlib` | Espressif zlib component 1.3.0 | zlib | required by the `libssh` and `libarchive` wrappers; `lib/telnet` only behind `HAVE_ZLIB`, which no build defines | dependency only |
| `bzip2` | bzip2 1.1.0 | bzip2 | required by the `libarchive` wrapper | dependency only |
| `liblzma` | xz liblzma 5.0.4 | public domain | required by the `libarchive` wrapper | dependency only |
| `lzma` | LZMA SDK, Igor Pavlov | public domain | required by the `libarchive` wrapper | dependency only |
| `zstd` | Zstandard 1.5.7 | BSD-3-Clause or GPL-2.0 | required by the `libarchive` wrapper | dependency only |
| `libarchive` | libarchive 3.9.0dev with an `esp32` compatibility layer | BSD-2-Clause | none in `lib/`, `src/` or `include/` | no consumer |
| `afpfs-ng` | Apple Filing Protocol client with an `esp32` port | GPL-2.0 | none | no consumer |
| `fsplib` | FSP v2 protocol client | permissive, see its `COPYING` | none | no consumer |
| `nlohmann` | nlohmann/json 3.12.0, header-only | MIT | none | no consumer |
| `sqlite3` | SQLite 3.50.4 with an `esp32` VFS | public domain | none | no consumer |

## How it fits
- `src/CMakeLists.txt` lists the components the main component links in its `PRIV_REQUIRES`
  (`expat`, `gumbo`, `gumbo-query`, `libssh`, `mlff` among the IDF ones); the rest are reached
  through `#include` and the wrappers' own `REQUIRES`.
- Components from the IDF registry (`led_strip`, `usb_host_cdc_acm`, `esp_websocket_client`) are
  declared in `src/idf_component.yml` and downloaded into `managed_components/`, with
  `dependencies.lock` pinning them. Both are generated; do not edit or commit them.
- The PC build uses [components_pc/](../components_pc/) instead, except that it compiles `gumbo`
  and `gumbo-query` from here and, for the LYNX target, `lz4`.

## Notes
Every directory listed as "no consumer" or "dependency only" is still compiled into every ESP32
build. A directory here keeps its upstream README and licence file where one was vendored; do not
add FujiNet documentation inside them.
