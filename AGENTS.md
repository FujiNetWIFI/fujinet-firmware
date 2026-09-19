# FujiNet development standards

FujiNet firmware: one C++20 codebase that builds network-adapter firmware for many retro computers
(Atari, Apple II, Coleco ADAM, CoCo, Commodore IEC, Lynx, RS232 and more) as an ESP32/ESP32-S3
ESP-IDF project driven by PlatformIO, plus a host "FujiNet-PC" binary built with CMake. These rules
apply repo-wide unless a section names a narrower path.

## Before you start

This file cannot tell you what is planned. Work is often staged as a series of topic branches
(`git branch -r` currently shows `heap-02-leaks` through `heap-05-internal-dram`, plus several
`*-bringup` branches), so the area you are about to change may already be in flight.

- Run `git log --oneline -20 -- <paths>` and `git branch -r` before touching an area. If recent work
  or an open branch overlaps, say so and ask before proceeding rather than duplicating or conflicting.
- For anything beyond a contained fix — a new abstraction, a new device or bus, a change to a shared
  base, a pattern you intend to repeat across platforms — describe the approach and get agreement
  before writing the code. A rejected design wastes far more of a reviewer's time than a question.
- If a maintainer says to hold off because another change is coming, stop. Do not rework the change
  to land it sooner.

## Build the firmware (ESP32)

There are two entry points: the `Makefile` for the common cases and `./build.sh` for the full flag
set. The `Makefile` wraps `build.sh` and strips ANSI colour codes from the output, so prefer it when
you need to read a build log.

```sh
make build        # == ./build.sh -b
make upload       # == ./build.sh -u    (firmware)
make uploadfs     # == ./build.sh -f    (LittleFS/webUI image)
make zip          # == ./build.sh -z
make clean        # == ./build.sh -c
make all          # == ./build.sh -a    (every board)
make pico-de-coco # builds pico/coco separately
```

Every `build.sh` run regenerates `platformio-generated.ini` by merging
`platformio-ini-files/platformio.common.ini` -> `build-platforms/platformio-<build_board>.ini` ->
`platformio.local.ini`, then calls `pio`.

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
make coco-lwm             # == ./build.sh -p COCO -g : host debug build, colour codes stripped
./build.sh -p ATARI       # configure, build, build dist/, then run ctest -V --progress
./build.sh -p ATARI -g    # same, debug build
(cd build/dist && ./run-fujinet)
```

The `%-lwm` pattern rule uppercases the name and adds `-g`, so `make atari-lwm`, `make apple-lwm`,
`make coco-lwm`, `make rs232-lwm`, `make lynx-lwm` and `make adam-lwm` are the six host builds.

This is the fastest real feedback available without hardware and the only path that runs tests.
Valid `-p` targets are exactly `ATARI`, `APPLE`, `COCO`, `RS232`, `LYNX`, `ADAM`; anything else is a
CMake `FATAL_ERROR`. A failing test aborts the whole invocation. Switching targets triggers an
automatic clean. Needs `cmake`, a C++20 compiler, MbedTLS 3.x, and `python_modules.txt`.

## Validate a change before proposing it

CI for the firmware is **build-only**: `autobuild.yml` compiles 10 ESP32 targets and runs no tests,
and the ctest suite runs in `build-fujinet-pc.yml` only because `build.sh -p` invokes it. No
workflow runs a linter or a formatter. So:

1. `make atari-lwm` (or `./build.sh -p ATARI`) — compiles host code and runs every ctest,
   including the policy check. Use the target matching the platform you changed.
2. `./build.sh -b` for at least one board of each platform your change touches.
3. `./build.sh -a` if you touched shared code under `lib/` or `src/main.cpp`.
4. `git diff` and re-read the comments in it: each must still be true of the code as changed.
5. `git diff --stat` and confirm every listed file is one you meant to change.

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

## Design principles

Reviewers hold changes to the direction below. A change that works but cuts against it will be sent
back, so raise the design first when yours does not fit.

- One implementation, many platforms. Behaviour common to two or more platforms belongs in the
  shared base; platform difference is expressed by overriding a virtual in the per-bus subclass, not
  by branching inside shared code. Duplicating a device into a new platform directory is the single
  most common rejection.
- Keep the layers apart: `lib/bus/<bus>/` owns wire protocol and timing, `lib/device/<bus>/` owns
  device behaviour, `lib/media/<platform>/` owns image formats. Do not reach across them, and do not
  put protocol details in a device or filesystem knowledge in a bus.
- Go through the existing abstractions — `fnConfig`, `FileSystem`/`fnFS`, `IOChannel`, the
  `network-protocol` adapters, `include/pinmap/` — rather than calling ESP-IDF or touching GPIO
  directly from a device. If an abstraction does not fit, propose extending it; do not bypass it.
- Adding a platform means a new bus/device directory plus a pinmap header, not edits scattered
  through shared files.
- Do not add global state or a new singleton. Hang state off the owning device or bus object.

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

## C++ and code structure

Full rules, with the worked `fujiDevice` example, are in **`docs/cpp-style.md`**. Read it
before writing device-side code or anything non-trivial. The rules broken most often:

- **Never return a bare `bool` for success or failure.** Return `success_is_true` or `error_is_true`
  from `include/global_types.h`, whichever the file already uses, via `RETURN_SUCCESS_AS_TRUE()`,
  `RETURN_ERROR_IF(expr)` and friends. Test the result only with `.is_success()` or `.is_error()` —
  never against `true`/`false`/`0`/`1` and never by truthiness, since the two types carry opposite
  polarity.
- **Compare `fujiError_t` against `FUJI_ERROR::NONE`, never `FUJI_ERROR::UNSPECIFIED`.** Use
  `if (err != FUJI_ERROR::NONE)` for failure and `== FUJI_ERROR::NONE` for success. `UNSPECIFIED` is
  a placeholder that is expected to be replaced by a real list of error codes, so any test written
  against it stops detecting failures the day the first new code lands.
- **Use the endian-sized types for anything on the wire**: `u16le_t`, `u24le_t`, `u32le_t`,
  `u16be_t`, `u24be_t`, `u32be_t`, and `u16ne_t`/`u24ne_t`/`u32ne_t` for native (all in
  `include/global_types.h`). Embed them in packed structs and pass the address of one straight to a
  read or write. No bit shifts over `uint8_t` arrays, no `htole*()`/`htobe*()`.
- **One capability per class; dispatch through a table, not a switch.** `Base64Mixin`, `HashMixin`,
  `QRMixin` and `AppKeyMixin` each own one feature and register their own commands. A new feature is
  a new mixin; a new command is one table entry plus one short handler. Vary behaviour by overriding
  a virtual, never by branching in shared code.
- **Own every resource and check every allocation**, including on error and teardown paths. Prefer
  `std::unique_ptr` or a container to a bare `new`/`delete` pair. Heap leaks on error paths are a
  recurring bug class here.
- **Exceptions are disabled in firmware builds** (`CONFIG_COMPILER_CXX_EXCEPTIONS` is unset), so a
  `throw` aborts the device at runtime. Return error types instead; do not add `try`/`catch` to
  firmware paths.

## Comments

Keep comments short. One or two lines above the code, or a brief trailing `//`. A multi-paragraph
block comment is almost always wrong here.

- Comment the *why* of a non-obvious choice — a timing constraint, a hardware quirk, a protocol
  requirement. Do not restate what the code already says.
- Do not narrate your own edit. No "added to fix X", no change logs, no dated notes, no
  before/after explanations, no `TODO` naming whoever wrote it. That belongs in the commit message
  and the PR, which is where reviewers look for it.
- Do not leave commented-out code. Delete it; git has it.
- Match the density of the file you are editing. If the surrounding functions carry no comments,
  adding a header block to yours makes the diff harder to review, not easier.

Before every commit, re-read each comment that appears in `git diff` — including ones you did not
write but whose code you changed. For each, confirm it is still **accurate** for the code as it now
stands, then that it is still brief and still says something the code does not. Fix or delete any
comment the change has falsified or made redundant. A comment that describes the old behaviour is
worse than no comment, and this is the easiest defect to leave behind.

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

## Scope of a change

One concern per pull request. Large mixed diffs are the most common reason a change is sent back.

- Do not combine a refactor with a behaviour change, a bug fix with a feature, or a rename with
  either. Land the refactor first, then the change that needed it.
- Keep formatting, whitespace and comment edits out of a functional diff entirely.
- A change that touches a shared base and then rolls the result out to several platforms is at least
  two pull requests: the base change, then the per-platform adoption.
- If a change cannot be explained in a couple of sentences, it is probably more than one change.
  Split it into a stacked series of small branches, as the `heap-02`..`heap-05` series did.
- When you find an unrelated problem mid-change, leave it and mention it. Do not fix it in passing.

## Commits and pull requests

- `master` is the main branch. Work on a topic branch; do not commit directly to `master`.
- The project squash-merges (0 merge commits in the last 300). GitHub appends the `(#NNNN)` suffix
  at squash time — **never type it yourself**. Use imperative mood: "Add foo", not "Added foo".
- An optional `[subsystem]` prefix is common (about half of recent commits) and can stack, e.g.
  `[config][rs232] Add default fnconfig.ini ...`. Observed tags: `[adam]`, `[iwm]`, `[coco]`,
  `[sio]`, `[rs232]`, `[atari]`, `[iec]`, `[lynx]`, `[apple2]`, `[drivewire]`, `[adamnet]`,
  `[network-protocol]`, `[fujiDevice]`, `[config]`, `[http]`, `[webui]`, `[all]`.
- `include/version.h` is bumped by hand at release time only; do not touch it incidentally.

When the change fixes a tracked issue, put `Fixes #NNNN` on its own line in the **pull request
description**. GitHub links the two straight away and closes the issue automatically when the PR
merges, so nobody has to go back and tidy up.

- `Fixes`, `Closes` and `Resolves` all work, as do their `-es`/`-ed` forms. One keyword per issue:
  `Fixes #1610, #1611` only closes the first, so write `Fixes #1610` and `Fixes #1611`.
- It belongs in the PR description, not only in a commit message. The squash commit's body is
  written from the PR, so that is the one place it reliably takes effect.
- Only use a closing keyword when the PR actually finishes the issue. For partial work write
  `Refs #NNNN` or `Part of #NNNN`, which links without closing.

## Common mistakes

- **Assuming Arduino.** This is ESP-IDF (`framework = espidf`). There is no `setup()`/`loop()`;
  the entry points are `app_main()` and `main()` in `src/main.cpp`. Arduino `String` is vestigial
  (8 uses in all of `lib/`).
- **Copying a device implementation per platform.** Extend `fujiDevice`/`NDevice` and override in
  the bus subclass instead. Adding `#ifdef BUILD_X` in the shared bases fails a ctest.
- **Editing generated files** such as `platformio-generated.ini` or `data/BUILD_*/www/`. The web UI
  is authored in `data/webui/{template,common,config}/` plus `lib/http/httpServiceParser.cpp`.
- **Committing sdkconfig churn** produced by a local build.
- **Bulk reformatting** a file because it does not match `.clang-format`.
- **Over-commenting.** Long explanatory block comments and edit narration inflate the diff and go
  stale; see Comments above.
- **One big diff.** Bundling a refactor, a fix and a rollout together; see Scope of a change.
- **Returning a bare `bool`** for success/failure, or testing a result with `if (result)` or
  `== true` instead of `.is_success()`/`.is_error()`.
- **Testing a `fujiError_t` against `FUJI_ERROR::UNSPECIFIED`** instead of `FUJI_ERROR::NONE`.
- **Hand-rolling endian conversion** with shifts or `htole*()` instead of the `u*le_t`/`u*be_t` types.
- **Leaving a comment that the change made untrue.**
- **Opening a PR that fixes a tracked issue without `Fixes #NNNN` in the description**, leaving the
  issue to be closed by hand.
- **Growing a god class or a giant switch** instead of adding a mixin, a handler-table entry, or an
  `NParser` subclass; see `docs/cpp-style.md`.
- **Building before asking.** Inventing an abstraction or a cross-platform pattern without agreeing
  the design first, when a branch already in flight may change it.
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

In-repo: `docs/cpp-style.md` (C++ and code structure, the companion to this file),
`build-sh.md` (canonical build-configuration guide, linked from `README.md`),
`build-platforms/README.md` (board INI format), `data/webui/README.md` (web UI generation).
`docs/build-sh-readme.md` duplicates `build-sh.md` and may drift; prefer `build-sh.md`. Off-repo:
the GitHub wiki carries the contribution process, development guidelines, the definition of done,
and the versioning/release procedure; `README.md` links the wiki, `https://fujinet.online/` and the
project Discord. Where this file is silent or disagrees with them, they win.
