# Build Script README

## Overview

This bash script serves as an interface for running PlatformIO builds for the FujiNet firmware project. It provides a flexible command-line interface to perform various build tasks, including cleaning, building, uploading firmware, and monitoring. The script also supports PC builds using CMake and offers options for setting up new boards and generating configuration files.

## Features

- Build firmware for various target platforms
- Clean build directories
- Upload firmware and filesystem
- Monitor serial output
- PC builds using CMake
- Setup new board configurations
- Generate and use custom INI files
- Dev mode and debug options
- Zip mode for creating flashable firmware packages

## Prerequisites

- Bash shell environment
- PlatformIO CLI
- Python (for some features)
- CMake (for PC builds)

## Usage

```
./build.sh [options] -- [additional args]
```

### Options

#### FujiNet Firmware (PlatformIO) Options

- `-c`: Run clean before build
- `-b`: Run build
- `-u`: Upload firmware
- `-f`: Upload filesystem (WebUI, etc.)
- `-m`: Run monitor after build
- `-d`: Add dev flag to build
- `-e ENV`: Use specific environment
- `-t TGT`: Run target task (default is none; `-b` must be specified for build)
- `-n`: Do not autoclean

#### One-off Firmware Options

- `-a`: Build ALL target platforms to test changes
- `-z`: Build flashable zip

#### FujiNet Firmware Board Setup Options

- `-s NAME`: Setup a new board, writes a new file 'platformio.local.ini'
- `-i FILE`: Use FILE as INI instead of platformio-generated.ini
- `-l FILE`: Use FILE instead of 'platformio.local.ini'

#### Companion MCU (RP2040/RP2350) Options

- `-P`: Build ONLY the companion (pico) firmware for the target board, then exit (does not run the ESP32 build). Resolved right after the target board/INI are settled, so `-s`/`-l`/`-i` are honoured. Equivalent to running `./build_pico.py <board> --ini <ini file>` directly. A board with no `[fujinet] pico_src` key is a no-op that exits 0.

See also the "Companion MCU (RP2040/RP2350) firmware" section below for the `[fujinet] pico_*` build keys and the `FUJINET_SKIP_PICO` environment variable.

#### FujiNet PC (CMake) Options

- `-c`: Run clean before build
- `-p TGT`: Perform PC build for given target (e.g., APPLE|ATARI)
- `-g`: Enable debug in generated FujiNet PC executable
- `-G GEN`: Use GEN as the Generator for CMake (e.g., -G "Unix Makefiles")

#### Other Options

- `-y`: Answer any questions with Y automatically (for unattended builds)
- `-h`: Display help information

### Examples

1. Clean and build current target:
   ```
   ./build.sh -cb
   ```

2. View FujiNet Monitor:
   ```
   ./build.sh -m
   ```

3. Clean, build, upload to FujiNet, and monitor:
   ```
   ./build.sh -cbum
   ```

4. PC build for ATARI target with debug enabled:
   ```
   ./build.sh -p ATARI -g
   ```

5. Setup a new board configuration:
   ```
   ./build.sh -s NEW_BOARD_NAME
   ```

## Configuration Files

The script uses two main configuration files:

1. `platformio-generated.ini`: Generated INI file containing build configurations
2. `platformio.local.ini`: Local INI file for user-specific settings

## Companion MCU (RP2040/RP2350) firmware

Some boards bundle a second, companion-MCU firmware image (e.g. `fujiversal-intv`, which
embeds the RP2040/RP2350 Minty cartridge firmware) inside the ESP32 build. This is driven
entirely by `[fujinet]` keys in that board's own `build-platforms/platformio-<board>.ini` --
no board-specific build scripts are needed:

- `pico_src` (and friends: `pico_repo`/`pico_repo_ref`, `pico_build`, `pico_board`,
  `pico_artifacts`, etc.) tell the shared `build_pico.py` `pre:` extra_script where the
  companion firmware's source lives, how to build it, and which output file(s) to bundle.
  Presence of `pico_src` is what enables the feature; a board with no `pico_src` gets an
  empty (stub) registry and pays no build cost. See the full key table and worked examples
  as a comment block in `platformio-ini-files/platformio.common.ini`, and
  `build-platforms/platformio-fujiversal-intv.ini` for a real one.
- `merge_bin` (and `merge_bin_name`) opt a board into `build_merge.py`, which folds the
  bootloader, partition table, and app image into a single flashable `<env>-merged.bin`.
  When set, `./build.sh -u` flashes that single file with `esptool.py` directly instead of
  PlatformIO's normal three-piece upload.

Use `./build.sh -P` to build just the companion firmware (skipping the ESP32 build
entirely) while iterating on it. Set `FUJINET_SKIP_PICO=1` to do the opposite -- skip the
companion firmware build during a normal ESP32 build and emit a stub instead. This matters
because `./build.sh -a` (and `build-platforms/build-all.sh`) walks every board ini in one
pass, and not every environment building "all boards" has the companion MCU toolchain (e.g.
`arm-none-eabi-gcc`) installed.

## Supported Boards

To view a list of supported boards, run:

```
./build.sh -S
```

## Troubleshooting

### Missing Python Modules

If you encounter errors related to missing Python modules, the script will attempt to install them automatically. If this fails, you can manually install the required modules using:

```
sh install_python_modules.sh
```

### INI File Issues

If you encounter an error about a missing local platformio INI file, you need to set up a new board configuration:

```
./build.sh -s BUILD_BOARD
```

Replace `BUILD_BOARD` with the desired board name from the supported boards list.

### Build Errors

1. Check that you have the latest version of PlatformIO installed.
2. Ensure all required dependencies are installed.
3. Try cleaning the build directory using the `-c` option before rebuilding.
4. Check the console output for specific error messages and address them accordingly.

### Upload Issues

1. Ensure your device is properly connected and recognized by your system.
2. Check that you have the correct upload port specified in your INI file.
3. Try unplugging and replugging your device before attempting the upload again.

### Monitor Problems

If the stacktrace doesn't work correctly in the monitor, ensure that the `build_board` value in `platformio.ini` matches the one in your generated INI file. You may need to manually update this value or copy the entire generated INI file over `platformio.ini`.

## Advanced Usage

### Additional Arguments

You can pass additional arguments to the underlying build processes by adding them after a double dash (`--`). For example:

```
./build.sh -p APPLE -- -DFOO=BAR
```

This passes the `-DFOO=BAR` argument to the CMake process for a PC build.

### Custom INI Files

You can use custom INI files for both the main configuration and local values:

```
./build.sh -i custom-platformio.ini -l custom-local.ini -cb
```

This allows for greater flexibility in managing different build configurations.

## Contributing

When contributing to the project, make sure to test your changes across multiple platforms using the `-a` option to build for all targets. This ensures compatibility across different boards and configurations.

## Support

If you encounter any issues not covered in this document, please refer to the project's issue tracker or community forums for additional support.