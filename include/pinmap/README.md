# include/pinmap

One header per board, each defining the `PIN_*` GPIO assignments for that hardware. The board ini
in `build-platforms/` passes exactly one `-D PINMAP_<BOARD>`, and each header is wrapped in the
matching `#ifdef`, so including all of them from `../pinmap.h` yields one set of definitions.

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
