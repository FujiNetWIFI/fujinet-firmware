# build.sh documentation

`build.sh` runs the PlatformIO build of the ESP32 firmware, the CMake build of FujiNet-PC, and the
companion-MCU firmware some boards embed. `./build.sh -h` prints every option.

It installs PlatformIO into a Python venv and offers to install missing firmware prerequisites,
including the companion-MCU toolchain (see [pico/](pico/)). A FujiNet-PC build (`-p`) also needs
CMake, a C++20 compiler and MbedTLS 3.x.

## Options

| Option | Does |
|---|---|
| `-b` | Build the firmware |
| `-c` | Clean before building (firmware or FujiNet-PC) |
| `-u` | Upload the firmware |
| `-f` | Upload the filesystem (the web UI) |
| `-m` | Run the serial monitor |
| `-z` | Build a flashable release zip |
| `-a` | Build every board |
| `-d` | Add the dev flag to the build |
| `-e ENV` | Use PlatformIO environment ENV |
| `-t TGT` | Run PlatformIO target TGT (`-b` is still needed to build) |
| `-n` | Do not autoclean |
| `-s NAME` | Write a new `platformio.local.ini` for board NAME |
| `-S` | List the board names |
| `-l FILE` | Use FILE instead of `platformio.local.ini` |
| `-i FILE` | Generate and use FILE instead of `platformio-generated.ini` |
| `-P` | Build only the companion-MCU firmware, then exit |
| `-p TGT` | Build FujiNet-PC for TGT (`ADAM`, `APPLE`, `ATARI`, `COCO`, `IEC`, `LYNX` or `RS232`) and run its tests |
| `-g` | Debug FujiNet-PC build |
| `-G GEN` | CMake generator, e.g. `-G "Unix Makefiles"` |
| `-V DIR` | Python venv location (or set `VENV_ROOT`) |
| `-y` | Answer yes to every question, including installing missing prerequisites |

Flags combine: `./build.sh -cbum` cleans, builds, uploads and monitors. Arguments after `--` go to
the underlying tool, so `./build.sh -p APPLE -- -DFOO=BAR` passes `-DFOO=BAR` to CMake.

`FUJINET_SKIP_TESTS=1` skips the tests after a `-p` build; `FUJINET_SKIP_PICO=1` skips the
companion-MCU firmware and embeds a stub.

## The local ini file

Every firmware build needs `platformio.local.ini`, which must set `build_board` in its
`[fujinet]` section. Board names are the `build-platforms/platformio-<board>.ini` files, or
`./build.sh -S`. To write a new one, overwriting any existing file:

```sh
./build.sh -s fujinet-atari-v1
```

Every run regenerates `platformio-generated.ini` by merging these files in order:

- `platformio-ini-files/platformio.common.ini`
- `build-platforms/platformio-<build_board>.ini`
- `platformio.local.ini`

so local changes survive upstream updates. git ignores `platformio.local*.ini` and
`platformio-release.ini`.

In the local file, `+=` appends to an inherited value and plain `=` replaces it:

```ini
[fujinet]
build_board = fujinet-atari-v1

[env]
upload_port = /dev/ttyOTHER
build_flags +=
        -D CORE_DEBUG_LEVEL=5
        -D VERBOSE_HTTP

[env:fujinet-atari-v1]
build_flags +=
        -D FN_HISPEED_INDEX=0
```

## Several boards

`-l` names a different local file, both when writing it with `-s` and when building:

```sh
./build.sh -ys fujinet-atari-v1 -l platformio.local-atari.ini
./build.sh -ys fujiapple-rev0 -l platformio.local-apple.ini

./build.sh -cbum -l platformio.local-atari.ini
./build.sh -cbum -l platformio.local-apple.ini
```

## Generating an ini without building

`-i` names the generated file. With no build flag (`-b`, `-p` or `-z`), the run writes it and exits:

```sh
./build.sh -i platformio-my-full-generated-file.ini -l platformio.local-atari-release.ini
```

Paths may be absolute or relative.

## Release zip

`-z` builds a flashable zip, applying `platformio-ini-files/platformio.zip-options.ini` over the
generated ini.

## Companion MCU (RP2040/RP2350) firmware

Some boards (the `fujiversal-*` ones) embed the cartridge firmware for an RP2040 or RP2350 in the
ESP32 image. `[fujinet]` keys in the board's own ini drive it:

- `pico_src` enables it; `pico_repo`, `pico_repo_ref`, `pico_build`, `pico_board`,
  `pico_artifacts` and the rest tell `build_pico.py` where the source lives, how to build it and
  what to embed. The full key table is a comment block in
  `platformio-ini-files/platformio.common.ini`; `build-platforms/platformio-fujiversal-intv.ini` is
  a real one. A board without `pico_src` gets an empty stub registry.
- A board that sets `pico_src` must also add `-D CONFIG_USB_PICOBOOT_HOST_ENABLED=1` to its
  `[env:<board>]` `build_flags`, which compiles in the updater that pushes the image to the
  companion at boot. `build_pico.py` fails the build if the two disagree.
- `merge_bin` (and `merge_bin_name`) fold the bootloader, partition table and app into one
  `<env>-merged.bin`, which `./build.sh -u` then flashes with `esptool.py`.

`-P` builds only the companion firmware. `FUJINET_SKIP_PICO=1` does the opposite, for a machine
without the companion toolchain; `-a` and `build-platforms/build-all.sh` set it per board when
that board's prerequisites are missing.

`fujiversal-drivewire` and `fujiversal-msx` build from the `pico/fujiversal` submodule. `build.sh`
initializes it when it is not checked out, and stops with the command to fix it when it is off its
pinned commit.

How the image reaches the companion at boot is in
[docs/fujiversal-flashing.md](docs/fujiversal-flashing.md).
