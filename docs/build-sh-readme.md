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