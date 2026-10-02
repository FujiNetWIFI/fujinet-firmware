# lib/ftp

An FTP client: a control connection plus a data connection per transfer, in active (`PORT`) or
passive (`PASV`, `EPSV`) mode.

## Layout
| File | Defines |
|---|---|
| `fnFTP.h`, `fnFTP.cpp` | `fnFTP`: login, directory listing, file read and write, rename, delete, mkdir and rmdir, with one method per FTP verb (`USER`, `PASS`, `CWD`, `LIST`, `RETR`, `STOR`, `RANG`, `SIZE`, `DELE`, `RNFR`/`RNTO`, `MKD`, `RMD`), reply-code classification helpers and `FTP_TIMEOUT` |

## How it fits
- Used by `FileSystemFTP` in [lib/FileSystem/](../FileSystem/) for host slots and by the `FTP:`
  adapter in [lib/network-protocol/](../network-protocol/).
- Both connections are `fnTcpClient` and `fnTcpServer` from [lib/tcpip/](../tcpip/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
