# lib/modem-sniffer

Logs the byte streams passing through an emulated modem to a file, so a session can be inspected
afterwards.

## Layout
| File | Defines |
|---|---|
| `modem-sniffer.h`, `modem-sniffer.cpp` | `ModemSniffer(FileSystem *, enable)`: `dumpInput`, `dumpOutput`, `closeOutput`, `setEnable`, `setActiveFS`; writes `SNIFFER_OUTPUT_FILE` on the given filesystem |

## How it fits
- Owned by every platform's modem device under [lib/device/](../device/) and enabled from the
  `[Modem]` config section that `lib/config/fnc_modem.cpp` reads.
- The dump file is written at the root of whichever filesystem the modem passes in, normally the
  SD card; the web file manager in [lib/http/](../http/) hides it along with FujiNet's other
  working files.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`.
