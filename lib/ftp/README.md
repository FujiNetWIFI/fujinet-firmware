# lib/ftp

An FTP client: a control connection plus a data connection per transfer, in active (`PORT`) or
passive (`PASV`, `EPSV`) mode.

## How it fits
- Used by `FileSystemFTP` in [lib/FileSystem/](../FileSystem/) for host slots and by the `FTP:`
  adapter in [lib/network-protocol/](../network-protocol/).
- Both connections are `fnTcpClient` and `fnTcpServer` from [lib/tcpip/](../tcpip/).

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
