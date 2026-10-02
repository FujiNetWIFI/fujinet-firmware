# include

Headers shared by every library and by `src/main.cpp`: the result and wire types, the command and
device ID enums, the debug macros and the per-board pin maps.

## Layout

| File | Defines |
|---|---|
| `global_types.h` | `ByteBuffer`; `fujiError_t` with `FUJI_ERROR::NONE`; the `success_is_true` and `error_is_true` result wrappers and their `RETURN_*` macros; the packed wire integers `u16le_t`, `u24le_t`, `u32le_t`, `u16be_t`, `u24be_t`, `u32be_t`; the native aliases `u16ne_t`, `u24ne_t`, `u32ne_t`, which are big-endian only under `BUILD_COCO` |
| `fujiCommandID.h` | `enum class CMD` (`fujiCommandID_t`): the `FUJI_`, `NET_`, `APETIME_`, `MODEM_`, `DISK_`, `CPM_`, `PCLINK_` and `PRINTER_` command families in one enum. Values overlap between families; the target device gives a byte its meaning |
| `fujiDeviceID.h` | `enum class FUJI_DEVICEID` (`fujiDeviceID_t`) with two maps: a small ADAM map and the Atari-SIO-style map every other platform uses, plus `operator+` for ranged IDs |
| `fuji_endian.h` | portable `htobe16` and friends over Linux, ESP-IDF, macOS, BSD and Windows (public domain); used by `global_types.h` |
| `global_defines.h` | product constants and EOL strings (from Meatloaf) |
| `debug.h` | `DEBUG`, set by `__PLATFORMIO_BUILD_DEBUG__`, `__PC_BUILD_DEBUG__` or `DBUG2`; `Debug_print`, `Debug_printf`, `Debug_println`, `Debug_printv`, `Debug_memory` and `HEAP_CHECK`, all no-ops without `DEBUG`, so never put a side effect in their arguments |
| `ansi_codes.h` | ANSI colour escapes used by `Debug_printv` |
| `pinmap.h` | ESP only: includes every [pinmap/](pinmap/) board header, then `pinmap_defaults.h` |
| `pinmap_defaults.h` | last-resort pin defaults (`PIN_DEBUG`, `LEDS_INVERTED`, the RGB LED strip) |
| `PSRAMAllocator.h` | ESP only: an STL allocator that tries `MALLOC_CAP_SPIRAM` first and `PSRAMDeleter` (Apache-2.0, Mike Dunston); use it for bulk buffers |
| `version.h` | `FN_VERSION_*`; listed in `.gitignore` but tracked, and bumped by hand at release time only |
| `atascii.h`, `petscii.h`, `cbm_defines.h` | Atari and Commodore character-set constants |
| `Log.h` | `LogFileOutput` shim for the AppleWin-derived SLIP code in `lib/devrelay` |
| `esp-idf-arduino.h` | Arduino compatibility shims; not included by any file |

## How it fits

- `global_types.h` carries the two rules reviewers enforce most: return `success_is_true` or
  `error_is_true` rather than a bare `bool`, and put `u16le_t`-style types in packed wire structs
  instead of shifting bytes. See [CONTRIBUTING.md](../CONTRIBUTING.md).
- `fujiCommandID.h` and `fujiDeviceID.h` are the vocabulary shared by every bus packet class in
  [lib/bus/](../lib/bus/) and every device in [lib/device/](../lib/device/).

## Build

On the include path of every ESP target (`src/CMakeLists.txt`) and every PC target
(`fujinet_pc.cmake`). `pinmap.h` and `PSRAMAllocator.h` compile to nothing without `ESP_PLATFORM`.
