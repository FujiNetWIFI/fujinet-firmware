# lib/network-protocol

Every URL scheme the N: network device can open, as `NetworkProtocol` subclasses that move bytes
through shared receive, transmit and special buffers.

## How it fits
- `NDevice` in [lib/device/NDevice/](../device/NDevice/) calls `createProtocol()` on OPEN and drives
  the result; its `NParser` subclasses read through the protocol for JSON, XML and HTML queries.
- `Protocol.h` includes `lib/bus/bus.h`, so a protocol can reach the active bus. `mail_draft` and
  `calendar_draft` depend only on the standard library and `fn_time` so the host tests can link
  them alone.
- Uses [lib/tcpip/](../tcpip/), [lib/TNFSlib/](../TNFSlib/), [lib/ftp/](../ftp/),
  [lib/webdav/](../webdav/), [lib/telnet/](../telnet/), [lib/runcpm/](../runcpm/),
  [lib/clipboard/](../clipboard/), the HTTP client from [lib/http/](../http/), and credentials
  from `Config`.

## Build
ESP: globbed by `src/CMakeLists.txt` into every target; `mgWebSocketClient.cpp` compiles to
nothing. PC: every adapter is listed in `fujinet_pc.cmake` for every target; `fnWebSocketClient.cpp`
is not. The `mail_tests` and `calendar_tests` targets in [tests/](../../tests/) compile the draft
parsers on their own.

## Notes
Schemes the factory accepts: CLIPBOARD, CPM, GCAL, GDRIVE, GMAIL, ICAL, WEBCAL, ICALH, IMAPS,
ONEDRIVE, TCP, UDP, TEST, TELNET, TNFS, FTP, HTTP, HTTPS, WS, WSS, SSH, SFTP, SSH.KEYGEN,
SSH.COPYID, SMB, NFS, S3, SD. JSON, XML and HTML are query layers in `NDevice`, not schemes.
