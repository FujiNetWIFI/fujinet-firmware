# lib/media/imd

An ImageDisk (`.imd`) reader and writer that depends on the C++ standard library only, so host
tools such as MekkoGX can build it as well as the firmware. Nothing from FujiNet or the ESP32 may
be included here.

## Layout

| File | Defines |
|---|---|
| `IMDImage.h`, `IMDImage.ipp` | `IMDImage<File>`, a class template over the file type it reads and writes, with `IMDStatus` and the format constants |
| `IMDImage.cpp` | the non-template helpers, and the `std::fstream` overloads of `imd_read_at`, `imd_write_at`, `imd_size` and `imd_flush` |

## How it fits

- FujiNet's adapter, `MediaTypeIMD` in [rs232/](../rs232/), uses `IMDImage<fnFile>`, with the
  `fnFile` overloads in `lib/media/IMDFnio.h` and a PSRAM memory resource for its sector tables.
- A host tool uses `IMDImage<std::fstream>`. Any other file type needs the same four overloads,
  declared before `IMDImage<File>` is used.

## Build

ESP: `src/CMakeLists.txt` globs `lib/media/**/*.cpp`, so this compiles into every target. PC:
`fujinet_pc.cmake` lists it for every target. `imdimage_tests` builds it with only this directory
on the include path, so a FujiNet or ESP32 include here fails that ctest.

## Notes

Format per IMD.TXT section 6, Dave Dunfield, placed in the public domain by its author.
