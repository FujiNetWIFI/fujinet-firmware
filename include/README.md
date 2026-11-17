# include

Headers shared by every library and by `src/main.cpp`: the result and wire types, the command and
device ID enums, the debug macros and the per-board pin maps.

## How it fits

- `global_types.h` carries the two rules reviewers enforce most: return `success_is_true` or
  `error_is_true` rather than a bare `bool`, and put `u16le_t`-style types in packed wire structs
  instead of shifting bytes. See [CONTRIBUTING.md](../CONTRIBUTING.md).
- `fujiCommandID.h` and `fujiDeviceID.h` are the vocabulary shared by every bus packet class in
  [lib/bus/](../lib/bus/) and every device in [lib/device/](../lib/device/).

## Build

On the include path of every ESP target (`src/CMakeLists.txt`) and every PC target
(`fujinet_pc.cmake`). `pinmap.h` and `PSRAMAllocator.h` compile to nothing without `ESP_PLATFORM`.

## Notes
- `version.h` is listed in `.gitignore` but tracked, and bumped by hand at release time only.
- `fuji_endian.h` is public domain.
- `PSRAMAllocator.h` is Apache-2.0 (Mike Dunston).
- `global_defines.h` comes from Meatloaf.
- Values in `fujiCommandID.h` overlap between command families; the target device gives a byte its
  meaning.
- `pinmap.h` and `PSRAMAllocator.h` are ESP only.
