# include/pinmap

One header per board, each defining the `PIN_*` GPIO assignments for that hardware. The board ini
in `build-platforms/` passes exactly one `-D PINMAP_<BOARD>`, and each header is wrapped in the
matching `#ifdef`, so including all of them from `../pinmap.h` yields one set of definitions.

## Layout

| File | Defines |
|---|---|
| `<board>.h` | the `PIN_*` macros for one board, guarded by `#ifdef PINMAP_<BOARD>`; for example `atariv1.h` (`PINMAP_ATARIV1`), `a2_rev0.h`, `fujiloaf-rev0.h`, `coco_devkitc.h`, `lynx-s3.h`, `rs232_s3.h`, `mac_rev0.h` and the `fujiversal-*.h` boards |
| `common.h` | `#ifndef` defaults for the SD card, UARTs, buttons and LEDs that a board header includes after its own definitions |
| `atari-common.h`, `coco-common.h`, `iec-common.h` | the same for the bus-specific signals of those platforms |

## How it fits

- [pinmap.h](../pinmap.h) includes every board header here and then `../pinmap_defaults.h`; buses
  and hardware wrappers in [lib/hardware/](../../lib/hardware/) and [lib/bus/](../../lib/bus/)
  use the `PIN_*` names and never raw GPIO numbers.
- `mcuconfig/` reads and rewrites these headers and inserts the `#include` into `../pinmap.h`, so
  a new board can be scaffolded from its TUI instead of by hand.
- `boards/capabilities.json` and `boards/buses.json` name the signal groups the TUI expects a
  board to define.

## Build

ESP only: `../pinmap.h` is empty without `ESP_PLATFORM`, and the PC build defines no `PINMAP_*`.
A header whose macro no board ini defines is dead code until a board uses it.
