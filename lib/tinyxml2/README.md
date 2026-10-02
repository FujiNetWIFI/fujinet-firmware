# lib/tinyxml2

tinyxml2 (Lee Thomason, zlib licence; the version is in `tinyxml2.h`), the XML parser behind the
N: device's XML queries, plus some unused add-ons.

## Layout
| File | Defines |
|---|---|
| `tinyxml2.h`, `tinyxml2.cpp` | tinyxml2, unmodified |
| `tinyhtml2.h`, `tinyhtml2.cpp` | an HTML front end for tinyxml2 (xuwf, idolpx fork); no consumer, HTML parsing moved to Gumbo |
| `tixml2ex.h`, `tixml2cx.h` | tinyxml2-ex iterator and copy helpers (Stan Thomas, MIT); no consumer |

## How it fits
- Used by [lib/fnxml/](../fnxml/) and by `tests/FnxmlQueryTests.cpp`.

## Build
ESP: globbed into every target, including the unused `tinyhtml2.cpp`. PC: `tinyxml2.cpp` only,
for every `FUJINET_TARGET`.
