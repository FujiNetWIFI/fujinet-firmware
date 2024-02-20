# build-platforms purpose

This folder contains the template files for creating a platformio ini file so that it can be built using build.sh.

Each file starting with `platformio-` is used for 2 things:

1. `build-all.sh` to check the compilation of that platform's code (this calls build.sh for each file found in this directory)
2. `build.sh` to create a platformio ini file for anyone referring to the build_board name in their `platformio.local.ini` (see [build.sh docs](../build-sh.md)) for more information on this.

## Format of ini files

Example file:

```ini
[fujinet]
build_platform = BUILD_ATARI
build_bus      = SIO
build_board    = fujinet-atari-v1

[env:fujinet-atari-v1]
platform = espressif32@${fujinet.esp32_platform_version}
platform_packages = ${fujinet.esp32_platform_packages}
board = fujinet-v1
build_type = debug
build_flags =
    ${env.build_flags}
    -D PINMAP_ATARIV1
```

There are always 2 sections needed, the first `[fujinet]` defines the 3 values required to uniquely define this board.

- `build_platform` is used in `fujinet-firmware` code for compiling.
- `build_bus` defines the type of bus used.
- `build_board` is the unique name that matches the name of the file after the `platformio-` part, and is used in a specific `[env:{build_board}]` section below it.

The second section is `[env:{build_board}]`. This defines the platform, packages, board, and other common values that this particular board needs to work. This will be included in every user's platformio configuration when they are building the board.
