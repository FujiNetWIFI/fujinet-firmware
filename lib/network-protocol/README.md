# lib/network-protocol

Every URL scheme the N: network device can open, as `NetworkProtocol` subclasses that move bytes
through shared receive, transmit and special buffers.

## Layout
| File | Defines |
|---|---|
| `Protocol.h`, `Protocol.cpp` | `NetworkProtocol` (abstract): `open`, `close`, `read`, `write`, `status`, `seek`, `available`; the `receiveBuffer`, `transmitBuffer` and `specialBuffer` pointers; `opened_url`; `ACCESS_MODE`; `NETPROTO_TRANS` end-of-line translation |
| `NetworkProtocolFactory.*` | `NetworkProtocolFactory::createProtocol()`: maps an upper-cased scheme to a protocol instance with a switch on `hash_djb2a` |
| `FS.h`, `FS.cpp` | `NetworkProtocolFS`: the template base for filesystem-like schemes; subclasses implement `mount`, `open_file_handle`, `open_dir_handle`, `read_file_handle`, `read_dir_entry`, `write_file_handle` and `stat`, and may add `rename`, `del`, `mkdir`, `rmdir`, `lock`, `unlock`; `dirFormat_t` |
| `HTTP.*`, `FTP.*`, `TNFS.*`, `SMB.*`, `NFS.*`, `SFTP.*`, `S3.*`, `SD.*`, `GDRIVE.*`, `ONEDRIVE.*` | `NetworkProtocolFS` subclasses: HTTP and HTTPS with WebDAV directories, FTP, TNFS, SMB (libsmb2), NFS (libnfs), SFTP (libssh), AWS S3, the local SD card, Google Drive and OneDrive through OAuth relays |
| `Mailbox.*`, `GMAIL.*`, `IMAPS.*`, `mail_draft.*` | `NetworkProtocolMailbox` (folders, messages, attachments) and its GMAIL and IMAPS subclasses; `MailDraft` parses RFC822-style compose text |
| `Calendar.*`, `GCAL.*`, `ICAL.*`, `calendar_draft.*` | `NetworkProtocolCalendar` (day, week, month and agenda views) and its GCAL and ICAL subclasses; `CalendarEventDraft` parses event text |
| `TCP.*`, `Telnet.*`, `UDP.*`, `WS.*`, `WSS.*` | Stream protocols: `NetworkProtocolTCP` and its `NetworkProtocolTELNET` subclass (libtelnet), `NetworkProtocolUDP`, `NetworkProtocolWS` and `NetworkProtocolWSS` |
| `SSH.*`, `SSHKeygen.*`, `SSHCopyId.*` | An interactive SSH session over libssh, key generation onto the SD card, and an ssh-copy-id equivalent |
| `CPM.*`, `CLIPBOARD.*`, `Test.*` | CP/M on the embedded RunCPM, the FujiNet clipboard, and a skeleton protocol |
| `fnWebSocketClient.*`, `mgWebSocketClient.*` | The WebSocket client over `esp_websocket_client` (ESP) or mongoose (PC), selected by `WS_CLIENT_CLASS` |
| `networkStatus.h`, `status_error_codes.h` | `NetworkStatus` and the `NDEV_STATUS` error codes |
| `text_format.h` | `fnfmt`: column helpers shared by the Mailbox and Calendar adapters |

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
