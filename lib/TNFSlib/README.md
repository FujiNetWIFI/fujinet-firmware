# lib/TNFSlib

Client library for TNFS, the Trivial Network File System that FujiNet hosts have spoken since the
beginning.

## How it fits
- `FileSystemTNFS` in [lib/FileSystem/](../FileSystem/) is the main consumer (host slots and the
  web file browser); on ESP its VFS shim registers TNFS mounts with ESP-IDF. The `TNFS:` adapter
  in [lib/network-protocol/](../network-protocol/) and the Meatloaf `tnfs` filesystem use it too.
- Transport is `fnUDP` or `fnTcpClient` from [lib/tcpip/](../tcpip/). A host name prefixed
  `_tcp.` or `_udp.` forces one; a failed TCP connect falls back to UDP.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.

## Notes
- `tnfs-protocol.md` is the protocol specification.
