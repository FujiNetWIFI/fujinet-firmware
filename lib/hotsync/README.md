# lib/hotsync

The desktop side of a Palm OS HotSync: a device that syncs with FujiNet gets the apps and databases
queued for it, and its databases are backed up.

## Layout
| File | Defines |
|---|---|
| `HotSyncLink.h` | `HotSyncLink`, the raw byte link a transport runs over (UART, socket, test fixture) |
| `Slp.h`, `Slp.cpp` | Serial Link Protocol framing and CRC |
| `PadpTransport.h`, `PadpTransport.cpp` | PADP fragments, ACK/retry, and the CMP handshake |
| `NetSyncTransport.h`, `NetSyncTransport.cpp` | NetSync framing and handshake |
| `DlpTransport.h` | `DlpTransport`, the request/response interface both transports implement |
| `Dlp.h`, `Dlp.cpp` | Desktop Link Protocol request and response encoding |
| `DlpClient.h`, `DlpClient.cpp` | typed DLP commands |
| `PalmDatabase.h`, `PalmDatabase.cpp` | `.pdb`/`.prc` files |
| `HotSyncSession.h`, `HotSyncSession.cpp` | one sync: identify, install, back up, stamp user info |
| `HotSyncStorage.h` | `HotSyncStorage`, the storage a session reads and writes |

## How it fits
- Depends only on `include/global_types.h`; nothing in the firmware uses it yet.
- `tests/HotSyncTests.cpp` drives it with frames captured from a Palm OS 3.3 device and with a fake
  device.
- Ported from [palm-sync](https://github.com/jichu4n/palm-sync) 0.2.1 (Apache-2.0); each header
  names the palm-sync source it follows.

## Build
ESP: not built. PC: not part of the firmware build; `tests/CMakeLists.txt` builds `hotsync_tests`
from these sources.

## Notes
- Backups are one way; there are no record-sync conduits (Memo, Address, Date Book).
- Installs and backups hold a whole database in RAM.
