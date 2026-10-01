# lib/webdav

Client-side parsers that turn a remote directory listing into entries: WebDAV PROPFIND XML and
Apache-style HTML index pages.

## Layout
| File | Defines |
|---|---|
| `WebDAV.h`, `WebDAV.cpp` | `WebDAV`: an expat parser for PROPFIND responses producing `DAVEntry` records (name, directory flag, size) |
| `IndexParser.h`, `IndexParser.cpp` | `IndexParser`: a hand-written parser for HTML index pages producing `IndexEntry` records (name, directory flag, size, modified time) |

## How it fits
- `NetworkProtocolHTTP` in [lib/network-protocol/](../network-protocol/) uses `WebDAV` to read
  directories over HTTP.
- `FileSystemHTTP` in [lib/FileSystem/](../FileSystem/) uses `IndexParser` for HTTP host slots.
- Not the WebDAV server; that is [lib/http/webdav/](../http/webdav/).

## Build
ESP: globbed by `src/CMakeLists.txt` into every target. PC: both files are listed in
`fujinet_pc.cmake` for every target.

## Notes
`IndexParser` does not use expat despite its header comment; it uses the string helpers from
[lib/utils/](../utils/).
