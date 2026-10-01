# lib/bus/mac

Macintosh 68k floppy-port bus for `BUILD_MAC`. The ESP32 never talks to the Mac directly: a
Raspberry Pi Pico running [pico/mac/](../../../pico/mac/) sits on the DB-19 port and emulates an
800K GCR floppy and HD20 hard disks in PIO, and the ESP32 feeds it over a 2 Mbaud UART with a
single-character command protocol. ESP32 only.

## Layout
| File | Defines |
|---|---|
| `mac.h`, `mac.cpp` | the Pico protocol (documented at the top of `mac.h`), `virtualDevice` (alias `macDevice`; devices override `process()`), the slot layout `MAC_DCD_SLOTS` and `MAC_FLOPPY_SLOT`, and `systemBus` with the floppy and DCD command handlers plus `add_dcd_mount()` and `rem_dcd_mount()` |
| `mac_ll.h`, `mac_ll.cpp` | `mac_ll` and `mac_floppy_ll`: streaming GCR track data out through the RMT peripheral |

## How it fits
- Selected by `BUILD_MAC` in [lib/bus/bus.h](../bus.h); devices live in
  [lib/device/mac/](../../device/mac/) and images in [lib/media/mac/](../../media/mac/).
- `FUJI_COMMAND_PACKET` is `FujiBusPacket` from [lib/bus/rs232/](../rs232/) only so that
  `fujiDevice` compiles; no Fuji commands travel over this bus and the `transaction_*` methods do
  nothing. The Mac has no CONFIG program; everything is driven from the web UI.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_MAC`. Not compiled on PC.

## Notes
`mac.h` and `mac.cpp` end with large `#if 0` blocks left from the IWM code this bus was copied
from.
