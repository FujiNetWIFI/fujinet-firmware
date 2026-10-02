# lib/utils

General helpers with no FujiNet state of their own: string and path handling, URL parsing, time
arithmetic, and a few small vendored pieces.

## Layout
| File | Defines |
|---|---|
| `utils.h`, `utils.cpp` | free `util_*` functions: trimming and case, `util_crunch` and the directory-entry formatters (`util_long_entry`, the Apple II CAT/CATALOG and ProDOS variants), wildcard matching, `util_tokenize`, devicespec fix-ups, PETSCII and ASCII conversion, `util_url_encode`, `util_html_escape`, `util_hexdump`, `util_sam_say` over [lib/sam/](../sam/), and `util_debug_printf` for PC logging |
| `string_utils.h`, `string_utils.cpp` | `namespace mstr` (starts/ends-with, trim, case, URL encode/decode, PETSCII/UTF-8, `format`) and the `hash_djb2a` / `_sh` string hash; from Meatloaf (GPL-3.0) |
| `peoples_url_parser.h`, `peoples_url_parser.cpp` | `PeoplesUrlParser`: scheme, user, password, host, port, path, name, extension, query, fragment; the type every protocol `open()` receives and the base of Meatloaf's `MFile`; from Meatloaf (GPL-3.0) |
| `fn_time.h`, `fn_time.cpp` | `namespace fn_time`: civil-date arithmetic, `fn_timegm`, a POSIX TZ string evaluator (`PosixTz`) and `parse_datetime`; written because MSVC's TZ handling is wrong |
| `punycode.h`, `punycode.cpp` | IDN punycode (Ben Noordhuis, MIT) |
| `U8Char.h`, `U8Char.cpp` | `U8Char`, UTF-8 to PETSCII; from Meatloaf |
| `cbuf.h`, `cbuf.cpp` | `cbuf` circular buffer from the esp8266 Arduino core (Ivan Grokhotkov, LGPL-2.1) |
| `endianness.h` | byte-order macros used by the Meatloaf D64 and T64 code |
| `std_extensions.hpp` | a `std::hash` specialisation for 4-byte arrays, used by the IWM SLIP bus |

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
Text sanitising for N: query results lives in [lib/fntext/](../fntext/), not here.
