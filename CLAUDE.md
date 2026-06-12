# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

FujiNet is a multi-function peripheral firmware for an ESP32-based device that connects vintage 8-bit computers (Atari, Apple II, CoCo, Commodore/IEC, ADAM, Lynx, RC2014, RS-232, and others) to modern networks. It emulates disk drives, printers, modems, clocks, and network adapters over each platform's native bus protocol.

There is also a **fujinet-PC** build that compiles the same firmware to run on a Linux/macOS/Windows host for development and testing, using CMake instead of PlatformIO.

## Build System

The project uses **PlatformIO** for ESP32 targets and **CMake** for the PC target. The primary interface is `build.sh`.

### Prerequisites

- PlatformIO CLI (`pio`)
- Python 3 with modules from `python_modules.txt` (`sh install_python_modules.sh`)
- CMake (for PC builds)
- `clang-format` (for the pre-commit coding-standard hook)

### First-time setup

Generate a `platformio.local.ini` for your board (board names come from `build-platforms/platformio-*.ini`):

```sh
./build.sh -s fujinet-atari-v1       # generates platformio.local.ini
./build.sh -s fujiapple-rev0 -l platformio.local-apple.ini  # named variant
```

The local file is git-ignored. The only required key is `[fujinet] build_board = <name>`.

### Common build commands

```sh
./build.sh -b          # build
./build.sh -cb         # clean then build
./build.sh -cbum       # clean, build, upload firmware, monitor serial
./build.sh -u          # upload firmware only
./build.sh -f          # upload filesystem (WebUI)
./build.sh -m          # open serial monitor

# PC (Linux/macOS) builds
./build.sh -p ATARI -g   # PC build for ATARI target with debug
./build.sh -p APPLE      # PC build for APPLE target
./build.sh -p COCO

# Other
./build.sh -a          # build ALL target platforms (CI smoke test)
./build.sh -z          # build flashable release ZIP
./build.sh -S          # list supported board names
```

Make targets are thin wrappers around `build.sh`: `make build`, `make upload`, `make uploadfs`, `make clean`, `make all`, `make atari-lwm`, `make apple-lwm`, `make coco-lwm`.

### INI file layering

Each build merges three files in order:
1. `platformio-ini-files/platformio.common.ini` — shared settings (do not edit)
2. `build-platforms/platformio-<board>.ini` — board-specific settings (do not edit)
3. `platformio.local.ini` — your local overrides (git-ignored, edit freely)

Use `+=` in the local file to append to existing `build_flags` rather than replace them:

```ini
[env]
build_flags +=
    -D CORE_DEBUG_LEVEL=5
    -D VERBOSE_HTTP
```

## Code Architecture

### Platform + bus abstraction

Every supported vintage computer has a **bus** (the hardware protocol) and a set of **devices** (peripherals that speak that protocol). The codebase separates these two concerns:

- `lib/bus/<bus>/` — bus controllers (e.g., `sio/`, `iec/`, `iwm/`, `drivewire/`, `adamnet/`, `comlynx/`)
- `lib/device/<bus>/` — device implementations per bus (e.g., `sio/disk.cpp`, `iec/drive.cpp`)
- `lib/media/<platform>/` — disk image format handling per platform (e.g., `atari/diskTypeAtr.*`, `apple/mediaTypeWOZ.*`)

`lib/bus/bus.h` and `lib/device/device.h` are thin dispatch headers: they `#include` the correct bus/device headers for the active `BUILD_*` define and declare the global device instances.

`src/main.cpp` contains `app_main()` (ESP32) or `main()` (PC). It instantiates `systemBus SYSTEM_BUS` and the configured device objects, wires them together, and starts the bus service loop.

### Shared libraries (platform-independent)

Everything under `lib/` that is not under `lib/bus/` or `lib/device/` is shared across all targets:

| Path | Purpose |
|------|---------|
| `lib/network-protocol/` | Protocol implementations (TCP, UDP, HTTP, TNFS, FTP, SMB, SSH, NFS, Telnet) exposed via the N: device |
| `lib/FileSystem/` | Virtual filesystem layer (SPIFFS/LittleFS flash, SD card, TNFS, SMB, FTP, NFS) |
| `lib/fuji/` | `fujiHost` / `fujiDisk` — host-slot and disk-slot management shared by all bus implementations |
| `lib/config/` | `fnConfig` — persistent configuration (split into `fnc_*.cpp` by subsystem) |
| `lib/hardware/` | HAL: `fnSystem`, `fnWiFi`, UART channels, LED, Bluetooth, timers |
| `lib/http/` | Built-in HTTP server (WebUI + API); ESP32 uses `httpService`, PC uses mongoose (`mgHttpService`) |
| `lib/tcpip/` | TCP/UDP socket wrappers |
| `lib/media/` | Disk image format parsers (platform-specific subdirs) |
| `lib/printer-emulator/` | Printer emulators (Epson, Atari, PDF, PNG, SVG, HTML) |
| `lib/task/` | `fnTask` / `fnTaskManager` — cooperative task system used by the PC build |
| `lib/modem/` | Hayes AT-command modem emulation |
| `lib/fnjson/` | JSON wrapper (used by N: and HTTP APIs) |

### ESP32 vs PC builds

The ESP32 build targets real hardware; the PC build (`-p TARGET`) compiles the same source with stub HAL implementations under `lib/hardware/` (`fnDummyWiFi`, `fnUARTUnix`, `fnSystemNet`, etc.) and uses mongoose for HTTP. Conditional compilation uses `#ifdef ESP_PLATFORM` / `#ifndef ESP_PLATFORM`.

`components/` holds ESP-IDF components for the ESP32 build. `components_pc/` holds third-party libraries used only by the PC build (mongoose, libnfs, libsmb2, libssh, etc.).

### Adding a new target platform

Use `fujinet.py` as a guide — it copies `lib/bus/.template` and `lib/device/.template`, then adds `#include` blocks to the dispatch headers. Create a `build-platforms/platformio-<new-board>.ini` entry.

## Coding Standards

- Style is enforced by `clang-format` using `.clang-format` (LLVM-based, 4-space indent, 95-column limit, no tabs).
- The pre-commit hook is `coding-standard.py`; install it with `./coding-standard.py --addhook`.
- Checked rules: no trailing whitespace, no tab alignment, clang-format compliance on changed C/C++ files.
- Run manually: `./coding-standard.py <file>` or `./coding-standard.py --fix <file>`.

## Key Defines

Build target is selected via `BUILD_*` and `BUILD_BUS` defines set in the board's PlatformIO ini:

- `BUILD_ATARI` / `BUILD_APPLE` / `BUILD_ADAM` / `BUILD_COCO` / `BUILD_IEC` / `BUILD_LYNX` / `BUILD_RS232` / `BUILD_RC2014` / etc.
- `ESP_PLATFORM` — defined automatically when targeting ESP32; absent for PC builds.

## Testing

Unit tests live in `test/`. Run them via PlatformIO's test runner (`pio test`). The CI workflow (`.github/workflows/autobuild.yml`) builds all platforms on every push/PR.
