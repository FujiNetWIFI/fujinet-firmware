# lib/TNFSlib

Client library for TNFS, the Trivial Network File System that FujiNet hosts have spoken since the
beginning.

## Layout
| File | Defines |
|---|---|
| `tnfs-protocol.md` | the protocol specification |
| `tnfslib.h` | the C-style API used by every consumer |
| `tnfslib.cpp` | `tnfs_mount`/`tnfs_umount`, `tnfs_open`/`tnfs_close`/`tnfs_read`/`tnfs_write`/`tnfs_lseek`; read caching |
| `tnfslib_dir.cpp` | `tnfs_opendirx`/`tnfs_readdirx` and the directory cache, `tnfs_mkdir`/`tnfs_rmdir`, `tnfs_stat`, `tnfs_unlink`, `tnfs_rename`, `tnfs_chmod`, `tnfs_size`/`tnfs_free`, `tnfs_chdir`/`tnfs_getcwd` |
| `tnfslib_transport.cpp` | the UDP and TCP transport code, the request/response transaction with retries, and session recovery |
| `tnfslib_codes.cpp` | packet debug dumps, command and result-code names, `tnfs_code_to_errno` |
| `tnfslib_internal.h` | declarations shared by the `tnfslib*.cpp` files; not part of the public API |
| `tnfslibMountInfo.h`, `tnfslibMountInfo.cpp` | `tnfsMountInfo` (host, session, `TNFS_PROTOCOL_TCP` or `TNFS_PROTOCOL_UDP`, open handles, directory cache) and `tnfsStat` |
| `tnfslib_udp.h`, `tnfslib_udp_testing.cpp` | the UDP send and receive hooks, with a replacement that simulates packet loss and duplication when `TNFS_UDP_SIMULATE_POOR_CONNECTION` is defined |
| `tnfslib_framing.h`, `tnfslib_framing.cpp` | `tnfs_frame_reply` and `tnfsReplyAssembler`: finds where each reply ends in a byte stream (TNFS has no length field); used by the TCP receive path |
| `tnfslib_stream_transport.h` | `tnfsStreamTransport`, the byte-stream connection a mount talks through |
| `tnfslib_stream_link.h`, `tnfslib_stream_link.cpp` | `tnfsStreamLink`: requests and replies over any `tnfsStreamTransport`; the framing, format, drop and reconnect rules |
| `tnfslib_tcp_stream.h`, `tnfslib_tcp_stream.cpp` | `tnfsTcpStream`: `tnfsStreamTransport` on `fnTcpClient` |

## How it fits
- `FileSystemTNFS` in [lib/FileSystem/](../FileSystem/) is the main consumer (host slots and the
  web file browser); on ESP its VFS shim registers TNFS mounts with ESP-IDF. The `TNFS:` adapter
  in [lib/network-protocol/](../network-protocol/) and the Meatloaf `tnfs` filesystem use it too.
- Transport is `fnUDP` or `fnTcpClient` from [lib/tcpip/](../tcpip/). A host name prefixed
  `_tcp.` or `_udp.` forces one; a failed TCP connect falls back to UDP.
- Over TCP, `tnfsStreamLink` runs the mount's `tnfsTcpStream`, and `tnfsReplyAssembler` cuts the
  byte stream into replies for the pending request: it
  checks session, sequence and command, settles a previous reply's optional fields (STAT uid
  and gid strings, a failed READ's or WRITE's count) by the header that follows them, skips
  earlier replies and TNFSD's sessionless ones, and never takes bytes received before the
  request went out as its answer. A malformed or ambiguous reply, a closed connection or a
  timeout with part of an answer received drops the connection, and the retry reconnects. The
  assembler exists only while TCP is connected; on ESP it is allocated with `PSRAMAllocator`
  (PSRAM first, then the default heap).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
