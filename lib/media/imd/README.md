# lib/media/imd

An ImageDisk (`.imd`) reader and writer that depends on the C++ standard library only, so host
tools such as MekkoGX can build it as well as the firmware. Nothing from FujiNet or the ESP32 may
be included here.

## Layout

| File | Defines |
|---|---|
| `IMDImage.h`, `IMDImage.cpp` | `IMDImage`; `IMDStream`, the byte storage it reads and writes through; `IMDStdioStream`, an `IMDStream` over a `FILE`; `IMDStatus` and the format constants |

## How it fits

- FujiNet's adapter, `MediaTypeIMD` in [rs232/](../rs232/), gives `IMDImage` an
  `IMDFnioStream` (`lib/media/IMDFnioStream.h`) and a PSRAM memory resource for its sector tables.
- A host tool gives it an `IMDStdioStream`, or its own `IMDStream`.

## Build

ESP: `src/CMakeLists.txt` globs `lib/media/**/*.cpp`, so this compiles into every target. PC:
`fujinet_pc.cmake` lists it for every target. `imdimage_tests` builds it with only this directory
on the include path, so a FujiNet or ESP32 include here fails that ctest.

## Notes

Format per IMD.TXT section 6, Dave Dunfield, placed in the public domain by its author.
