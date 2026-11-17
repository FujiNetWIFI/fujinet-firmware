# lib/utils

General helpers with no FujiNet state of their own: string and path handling, URL parsing, time
arithmetic, and a few small vendored pieces.

## How it fits
- `utils.h` is included almost everywhere: buses, devices, config, FileSystem, http, media,
  network protocols and printers. `include/debug.h` routes PC `Debug_*` output to
  `util_debug_printf`.
- `mstr::` and `hash_djb2a` drive the scheme switch in `NetworkProtocolFactory`;
  `PeoplesUrlParser` is the URL type throughout [lib/network-protocol/](../network-protocol/) and
  [lib/meatloaf/](../meatloaf/); `fn_time` backs the calendar adapters.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET`; `std_extensions.hpp` only with APPLE.
The `calendar_tests` ctest links `fn_time.cpp` on its own.

## Notes
Text sanitizing for N: query results lives in [lib/fntext/](../fntext/), not here.

- `string_utils.*` and `peoples_url_parser.*` come from Meatloaf (GPL-3.0), as does `U8Char.*`.
- `punycode.*` is Ben Noordhuis's (MIT); `cbuf.*` is from the esp8266 Arduino core (Ivan
  Grokhotkov, LGPL-2.1).
