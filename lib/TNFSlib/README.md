# lib/TNFSlib

Client library for TNFS, the Trivial Network File System that FujiNet hosts have spoken since the
beginning.

## Layout
| File | Defines |
|---|---|
| `tnfs-protocol.md` | the protocol specification |
| `tnfslib.h`, `tnfslib.cpp` | the C-style API: `tnfs_mount`, `tnfs_opendirx`/`tnfs_readdirx`, `tnfs_open`/`tnfs_read`/`tnfs_write`/`tnfs_lseek`, `tnfs_stat`, `tnfs_rename`, `tnfs_unlink`, `tnfs_mkdir`, `tnfs_chdir`, `tnfs_size`, `tnfs_free`; read caching; the UDP and TCP transport code |
| `tnfslibMountInfo.h`, `tnfslibMountInfo.cpp` | `tnfsMountInfo` (host, session, `TNFS_PROTOCOL_TCP` or `TNFS_PROTOCOL_UDP`, open handles, directory cache) and `tnfsStat` |
| `tnfslib_udp.h`, `tnfslib_udp_testing.cpp` | the UDP send and receive hooks, with a replacement that simulates packet loss and duplication when `TNFS_UDP_SIMULATE_POOR_CONNECTION` is defined |

## How it fits
- `FileSystemTNFS` in [lib/FileSystem/](../FileSystem/) is the main consumer (host slots and the
  web file browser); on ESP its VFS shim registers TNFS mounts with ESP-IDF. The `TNFS:` adapter
  in [lib/network-protocol/](../network-protocol/) and the Meatloaf `tnfs` filesystem use it too.
- Transport is `fnUDP` or `fnTcpClient` from [lib/tcpip/](../tcpip/). A host name prefixed
  `_tcp.` or `_udp.` forces one; a failed TCP connect falls back to UDP.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
