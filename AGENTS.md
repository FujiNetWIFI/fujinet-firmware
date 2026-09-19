# AGENTS.md

FujiNet firmware: one C++20 codebase that builds network-adapter firmware for many retro computers
(Atari, Apple II, Coleco ADAM, CoCo, Commodore IEC, Lynx, RS232 and more) as an ESP32/ESP32-S3
ESP-IDF project driven by PlatformIO, plus a host "FujiNet-PC" binary built with CMake. These rules
apply repo-wide unless a section names a narrower path.

## Build the firmware (ESP32)

`./build.sh` is the only supported entry point. Every run regenerates `platformio-generated.ini` by
merging `platformio-ini-files/platformio.common.ini` ->
`build-platforms/platformio-<build_board>.ini` -> `platformio.local.ini`, then calls `pio`.

```sh
./build.sh -y -s fujinet-atari-v1   # once per checkout: writes platformio.local.ini
./build.sh -b                       # build
./build.sh -cb                      # clean + build
./build.sh -cbum                    # clean, build, upload firmware, monitor
./build.sh -S                       # list every supported board name
./build.sh -a                       # build all 29 boards; results in build-results.txt
```

- `platformio.local.ini` is git-ignored and absent on a fresh clone. Without it every build exits
  with an error. `-s <board>` overwrites it; pass `-y` to skip the confirmation prompt.
- The active target comes solely from `build_board` in the `[fujinet]` section of
  `platformio.local.ini`, which names `build-platforms/platformio-<build_board>.ini`.
- Flags combine in any order. `-f` is **upload filesystem** (`uploadfs`, the LittleFS/webUI image),
  not "flash". `-u` uploads firmware. `-z` builds a flashable release zip. `-n` disables autoclean.
  After a `data/webui/` change, `-u` alone is not enough; `-f` is required.

## Build and test on the host (FujiNet-PC)

```sh
./build.sh -p ATARI        # configure, build, build dist/, then run ctest -V --progress
./build.sh -p ATARI -g     # same, debug build
(cd build/dist && ./run-fujinet)
```

This is the fastest real feedback available without hardware and the only path that runs tests.
Valid `-p` targets are exactly `ATARI`, `APPLE`, `COCO`, `RS232`, `LYNX`, `ADAM`; anything else is a
CMake `FATAL_ERROR`. A failing test aborts the whole invocation. Switching targets triggers an
automatic clean. Needs `cmake`, a C++20 compiler, MbedTLS 3.x, and `python_modules.txt`.

## Validate a change before proposing it

CI for the firmware is **build-only**: `autobuild.yml` compiles 10 ESP32 targets and runs no tests,
and the ctest suite runs in `build-fujinet-pc.yml` only because `build.sh -p` invokes it. No
workflow runs a linter or a formatter. So:

1. `./build.sh -p ATARI` — compiles host code and runs every ctest, including the policy check.
2. `./build.sh -b` for at least one board of each platform your change touches.
3. `./build.sh -a` if you touched shared code under `lib/` or `src/main.cpp`.
4. `git diff --stat` and confirm every listed file is one you meant to change.

## Tests

- The live suite is `tests/`, doctest-based, wired in only for the PC build. ctest names:
  `fujibuspacket_tests`, `calendar_tests`, `mail_tests`, `no_build_ifdefs_in_fujidevice`, and
  `sio_dstats_tests` (built only when `FUJINET_TARGET=ATARI`).
- Run them with `./build.sh -p ATARI`, or `ctest -V` from `build/` after a PC configure.
- To add one, add an `add_executable` + `add_test` pair to `tests/CMakeLists.txt`; keep the unit
  under test free of hardware and FujiNet globals so it links alone, as `fn_time.cpp` does.
- `test/` (singular) is the stale PlatformIO/Unity on-hardware directory; nothing references it.
  Do not extend it and do not add tests there.

## Enforced architecture rule

No `BUILD_*` identifier may appear in a `#if`, `#ifdef`, `#ifndef` or `#elif` anywhere under
`lib/device/fujiDevice`, `lib/device/fujiClock`, or `lib/device/NDevice`. `tests/check_no_build_ifdefs.py`
enforces this as the `no_build_ifdefs_in_fujidevice` ctest; it strips comments and string literals
first, so you cannot dodge it. Customize by overriding a virtual in the per-bus subclass under
`lib/device/<bus>/`, or by a mixin in `lib/device/fujiDevice/`.

The codebase is actively unifying per-platform code into these shared bases: `fujiDevice` (plus its
mixins) and `NDevice` replaced the per-bus Fuji and network devices. Extend the shared layer rather
than copying an existing device implementation into a new platform directory.

## Platform conditional compilation

- Platform macros are `BUILD_ADAM`, `BUILD_APPLE`, `BUILD_ATARI`, `BUILD_COCO`, `BUILD_CX16`,
  `BUILD_H89`, `BUILD_IEC`, `BUILD_LYNX`, `BUILD_MAC`, `BUILD_RC2014`, `BUILD_RS232`, `BUILD_S100`
  (plus `NEW_TARGET`, the skeleton port). Exactly one is defined per build. They come from
  `build_platform` in `build-platforms/platformio-<board>.ini`, or from `fujinet_pc.cmake`.
- Firmware vs host is `#ifdef ESP_PLATFORM` / `#ifndef ESP_PLATFORM`. There is **no** `FUJINET_PC`
  macro; `FUJINET_TARGET` is a CMake variable, not a preprocessor symbol. Board/hardware variants
  are a separate `PINMAP_*` family, one per header in `include/pinmap/`.
- `lib/bus/bus.h`, `lib/device/device.h` and `lib/device/disk.h` are the `#ifdef BUILD_*`
  switchboards that select the bus, the device set and the `DISK_DEVICE` alias.

## Never hand-edit or commit these

- `platformio-generated.ini` — rewritten on every `build.sh` run; edits are silently destroyed.
- `platformio.ini`, `platformio-ini-files/platformio.common.ini` (DO NOT EDIT banner),
  `managed_components/`, `.pio/`, `build/`, `firmware/`, `dependencies.lock`, `data/BUILD_*/`.

Put personal board settings, extra `build_flags`, monitor port and debug defines in the git-ignored
`platformio.local.ini`. There `+=` appends to an inherited value and plain `=` replaces it; using
`=` on `build_flags` silently drops the shared platform defines.

The sdkconfig trap: `sdkconfig.*` and `include/version.h` are in `.gitignore` but **tracked**, so
the ignore has no effect. PlatformIO/ESP-IDF rewrites `sdkconfig.<env>` on essentially every
firmware build and it shows up dirty in `git status`. Never sweep an incidental `sdkconfig.*` diff
into an unrelated commit; `git checkout --` it. A deliberate sdkconfig change (flash size, PSRAM
mode, socket counts) is normal and correct to commit. Read `git diff --stat` before staging.

## Scripts that are broken or obsolete

- `verify-webui.sh`, `verify-webui-progress.sh`, `full-verify.sh` — one-off scripts from an
  already-merged PR. They `git checkout consolidate-webui` and `full-verify.sh` also stashes your
  work. Do not run them; `verify-webui.sh` is additionally broken.
- `fujinet.py` is an empty stub. `.travis.yml`, `platformio-sample.ini` and the README's
  "MAJOR ANNOUNCEMENT" are dead legacy; do not fix them as part of unrelated work.

## Code style

`.clang-format` exists (LLVM base, `IndentWidth: 4`, `ColumnLimit: 95`, `UseTab: Never`, Allman
braces, `SortIncludes: false`, `IncludeBlocks: Preserve`), but nothing enforces it and much of the
tree does not conform.

- Match the surrounding file. Never bulk-reformat, and never reformat lines your change does not
  otherwise touch. Do not reorder or sort includes; the formatter is configured not to.
- `coding-standard.py` checks only trailing whitespace and tabs today (its clang-format path is
  hard-disabled) and no workflow invokes it. `./coding-standard.py --addhook` installs it locally.
- Use `std::string`, not Arduino `String`. Prefix new private members with `_`. Use fixed-width
  types and `__attribute__((packed))` structs for wire formats, never `std::string`.

## Logging

Use `Debug_print`, `Debug_printf`, `Debug_println`, `Debug_printv`, `Debug_memory()` and
`HEAP_CHECK(x)` from `include/debug.h`. They compile to nothing unless `DEBUG` is defined (set by
`__PLATFORMIO_BUILD_DEBUG__`, `__PC_BUILD_DEBUG__` or `DBUG2`), so never put a side effect inside a
`Debug_*` argument. Extra verbosity is compile-time and per-subsystem: `VERBOSE_SIO`, `VERBOSE_TNFS`,
`VERBOSE_DISK`, `VERBOSE_HTTP`, `VERBOSE_ATX`, `VERBOSE_PROTCOL` (spelled that way).

The main loop has no delay, so per-iteration output is unreadable at bus rates. Do not add
unconditional logging to `fn_service_loop`, a `systemBus::service` path, or an ISR; rate-limit it
with a `static` timestamp the way the 10-second heap report in `src/main.cpp` does. `HEAP_CHECK`
walks the whole heap and is a temporary diagnostic only.

## Memory and timing

- The scarce resource is **internal DRAM**, not total heap; exhaustion has been observed with PSRAM
  sitting idle. Prefer `include/PSRAMAllocator.h` or `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for
  bulk buffers; reserve `MALLOC_CAP_INTERNAL`/`MALLOC_CAP_DMA` for buffers hardware requires.
- Do not add allocation, logging, or calls into non-IRAM code on a bus ISR or hot path. Anything
  reachable from one must be `IRAM_ATTR`; `lib/bus/iec/IECBusHandler.cpp` `#error`s if it is
  undefined on ESP. Use `volatile` for ISR/task-shared state.
- FreeRTOS stack sizes are hand-tuned literals per task (1024 to 32768). Do not change one without
  a measured reason; check the return value of new `xTaskCreate*` calls.
- Prefer static or pooled storage over new allocation in bus and device paths, and check every
  allocation result on error paths.

## Commits and pull requests

- `master` is the main branch. Work on a topic branch; do not commit directly to `master`.
- The project squash-merges (0 merge commits in the last 300). GitHub appends the `(#NNNN)` suffix
  at squash time — **never type it yourself**. Use imperative mood: "Add foo", not "Added foo".
- An optional `[subsystem]` prefix is common (about half of recent commits) and can stack, e.g.
  `[config][rs232] Add default fnconfig.ini ...`. Observed tags: `[adam]`, `[iwm]`, `[coco]`,
  `[sio]`, `[rs232]`, `[atari]`, `[iec]`, `[lynx]`, `[apple2]`, `[drivewire]`, `[adamnet]`,
  `[network-protocol]`, `[fujiDevice]`, `[config]`, `[http]`, `[webui]`, `[all]`.
- `include/version.h` is bumped by hand at release time only; do not touch it incidentally.

## What agents get wrong here

- **Assuming Arduino.** This is ESP-IDF (`framework = espidf`). There is no `setup()`/`loop()`;
  the entry points are `app_main()` and `main()` in `src/main.cpp`. Arduino `String` is vestigial
  (8 uses in all of `lib/`).
- **Copying a device implementation per platform.** Extend `fujiDevice`/`NDevice` and override in
  the bus subclass instead. Adding `#ifdef BUILD_X` in the shared bases fails a ctest.
- **Editing generated files** such as `platformio-generated.ini` or `data/BUILD_*/www/`. The web UI
  is authored in `data/webui/{template,common,config}/` plus `lib/http/httpServiceParser.cpp`.
- **Committing sdkconfig churn** produced by a local build.
- **Bulk reformatting** a file because it does not match `.clang-format`.
- **Assuming CI tests the firmware.** It only compiles it.

## Report what you did not do

Almost nothing here can be verified without a device. When you report work, state plainly what was
not done rather than implying it was: which targets you built and which you did not, that you did
not flash or run on hardware, that no test covers the change.

## Where to look

- `src/` holds only `main.cpp` (per-platform device assembly); nearly all code is in `lib/`.
- `lib/bus/<bus>/` protocol and `systemBus`; `lib/device/<bus>/` that bus's devices;
  `lib/media/<platform>/` disk-image formats; `lib/device/fujiDevice`, `NDevice`, `fujiClock` the
  shared bases; `lib/fuji/` host and disk-slot model.
- Cross-cutting: `lib/config/` (`fnConfig`, one `fnc_<section>.cpp` per INI section),
  `lib/FileSystem/`, `lib/http/`, `lib/network-protocol/`, `lib/hardware/`.
- `include/pinmap/` one header per board, guarded by its `PINMAP_*` macro.
- `components/` and `components_pc/` are vendored; `pico/` is separate RP2040 firmware.
- `git log --oneline -20 -- <path>` is the quickest way to learn a directory's local convention.

## Authoritative sources

In-repo: `build-sh.md` (canonical build-configuration guide, linked from `README.md`),
`build-platforms/README.md` (board INI format), `data/webui/README.md` (web UI generation).
`docs/build-sh-readme.md` duplicates `build-sh.md` and may drift; prefer `build-sh.md`. Off-repo:
the GitHub wiki carries the contribution process, development guidelines, the definition of done,
and the versioning/release procedure; `README.md` links the wiki, `https://fujinet.online/` and the
project Discord. Where this file is silent or disagrees with them, they win.
