# lib/webdav

Client-side parsers that turn a remote directory listing into entries: WebDAV PROPFIND XML and
Apache-style HTML index pages.

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
