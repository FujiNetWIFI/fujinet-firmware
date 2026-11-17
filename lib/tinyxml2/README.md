# lib/tinyxml2

tinyxml2 (Lee Thomason, zlib license; the version is in `tinyxml2.h`), the XML parser behind the
N: device's XML queries, plus some unused add-ons.

## How it fits
- Used by [lib/fnxml/](../fnxml/) and by `tests/FnxmlQueryTests.cpp`.

## Build
ESP: globbed into every target, including the unused `tinyhtml2.cpp`. PC: `tinyxml2.cpp` only,
for every `FUJINET_TARGET`.

## Notes
- `tinyxml2.h` and `tinyxml2.cpp` are unmodified.
- `tinyhtml2.h`/`tinyhtml2.cpp` (an HTML front end, xuwf, idolpx fork) and
  `tixml2ex.h`/`tixml2cx.h` (tinyxml2-ex helpers, Stan Thomas, MIT) have no consumer.
