# pico

Firmware for the RP2040 or RP2350 co-processor that sits in a retro machine's cartridge slot or
on its floppy port and relays bus traffic to the ESP32 running FujiNet.

## Layout
| Directory | Target | Build | Embedded by `build_pico.py` | ESP32 side | README |
|---|---|---|---|---|---|
| `intellivision/` | Intellivision cartridge on the RP2354A `fujicard` board; a fork of Minty (GPLv3, see `intellivision/PROVENANCE.md`) | pico-sdk CMake in `intellivision/firmware/` | yes, for `fujiversal-intv` | [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | yes |
| `fujiversal/` | Generic ROM-emulating cartridge for the CoCo and MSX, RP2350B; a git submodule | upstream Makefile, or CMake with the board passed as `BOARD` | yes, for `fujiversal-drivewire` and `fujiversal-msx` | [lib/bus/drivewire/](../lib/bus/drivewire/) or [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | upstream `fujiversal/readme.md` |
| `fujiversal-roms/msx/` | Prebuilt MSX CONFIG ROM that the `fujiversal-msx` cartridge serves | not built here (needs z88dk) | input to the `fujiversal-msx` prebuild step | | yes |
| `astrocade/` | Bally Astrocade cartridge on an RP2040 `fujicade` board | `astrocade/build.sh` (Z80 clients) and `astrocade/build-cart.sh` (pico-sdk) | no | [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | yes |
| `arcadia/` | Emerson Arcadia 2001 cartridge, RP2040 | `arcadia/build.sh` (2650 clients, Macroassembler AS) and `arcadia/build-cart.sh` (pico-sdk) | no | [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | yes |
| `o2/` | Odyssey 2 and Videopac cartridge, RP2040; a fork of PicoPAC | `o2/build.sh` (8048 clients) and `o2/build-cart.sh` (pico-sdk) | no | [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | yes |
| `nes/` | NES cartridge on an RP2354B with two 512K SRAMs for PRG and CHR | `nes/build.sh` (6502 clients, cc65) and `nes/build-cart.sh` (pico-sdk) | no | [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | yes |
| `common/` | FujiBus codec, its USB CDC transport and a CDC-only USB device, shared by the cartridges | compiled into each cart's pico-sdk build | no | [lib/bus/rs232/](../lib/bus/rs232/) over USB CDC | [common/](common/) |
| `atari-2600/` | Atari 2600 PlusCart-Pico port, RP2040 | PlatformIO per its README; no project ini is tracked | no | none wired in | upstream README |
| `coco/` | Early CoCo cartridge, RP2040 | pico-sdk CMake | no | [lib/bus/drivewire/](../lib/bus/drivewire/) over a PIO UART | [coco/](coco/) |
| `mac/` | Macintosh 68k floppy-port emulation on the `fujimac-rev0` board, RP2040 | pico-sdk CMake | no | [lib/bus/mac/](../lib/bus/mac/) over a 2 Mbaud UART | [mac/](mac/) |
| `tools/rom2h.py` | Renders a ROM image as a C header, replacing `xxd -i` | | run by `pico_prebuild` for the fujiversal boards | | |

## How it fits
- On a Fujiversal board the ESP32-S3 is the USB host and the cartridge is a CDC device. FujiBus
  packets (`FujiBusPacket`) travel over that link into the RS232 or DriveWire bus; the ESP32 side
  is `ACMChannel` and `fnUsbHost` in [lib/hardware/](../lib/hardware/).
- The cartridge image is built by `build_pico.py` during the ESP32 build, written as
  `fn_pico_blob_data.cpp` into the env's build directory (`src/CMakeLists.txt` adds it to the
  sources), and flashed over PICOBOOT at boot by `fnPicoUpdater` when it differs from the image
  recorded in NVS. `docs/fujiversal-flashing.md` describes the sequence;
  the `pico_*` keys that drive it are documented in `platformio-ini-files/platformio.common.ini`.
- `fujiversal/` is a git submodule; `build_pico.py` checks it out if it is missing.

## Build
A board ini with `pico_src` set needs an ARM GCC toolchain with newlib and libstdc++, CMake, Ninja
and a pico-sdk checkout with its tinyusb submodule (`PICO_SDK_PATH`), or `FUJINET_SKIP_PICO=1` to
build the ESP32 side alone. `build.sh` checks for them before it starts the build and offers to install
what it can; the versions are the `pico_*` keys in `platformio-ini-files/platformio.common.ini`. The other directories
build standalone with their own scripts and are exercised by the pico cartridges workflow.
`make pico-de-coco` at the repository root runs the CoCo cart's already-configured build directory.

## Notes
Every `build` directory under here is generated and git-ignored.
