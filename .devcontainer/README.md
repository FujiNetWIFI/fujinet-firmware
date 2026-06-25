# Dev Container for FujiNet Firmware

This folder lets you build the FujiNet firmware **without installing any
toolchain on your machine**. Everything — PlatformIO, the ESP32 toolchain,
CMake, g++, the retro cross-compilers — lives inside the
[defoogi](https://github.com/FozzTexx/defoogi) container. You clone the repo,
open it in the container, and run `./build.sh`.

The same configuration works two ways:

- **Locally** with VS Code + Docker ("Reopen in Container")
- **In the cloud** with GitHub Codespaces ("Create codespace on this branch")

## Quick start (local)

1. Install [Docker](https://docs.docker.com/get-docker/) and VS Code with the
   **Dev Containers** extension (`ms-vscode-remote.remote-containers`).
2. Open this repo in VS Code.
3. Command Palette → **Dev Containers: Reopen in Container**. First build pulls
   the image and provisions the container.
4. In the integrated terminal:
   ```sh
   ./build.sh -s fujinet-atari-v1   # one-time: pick your board
   ./build.sh -b                    # compile firmware
   ./build.sh -p ATARI              # or compile the PC build
   ```

## Quick start (Codespaces)

1. On GitHub → **Code ▸ Codespaces ▸ Create codespace**.
2. Wait for it to build, then use the same `./build.sh` commands above.

## What works where

The container can **compile** anything. Talking to **real hardware over USB**
(flashing, serial monitor) only works locally, because Codespaces is a cloud VM
with no USB.

| Task                                              | Local | Codespaces |
| ------------------------------------------------- | :---: | :--------: |
| PC build (`./build.sh -p ATARI`)                  |   ✅   |     ✅      |
| Compile ESP32 firmware (`./build.sh -b`)          |   ✅   |     ✅      |
| Upload / flash a real ESP32 (`-u`, `-cbum`)       |  ✅*   |     ❌      |
| Serial monitor (`-m`)                             |  ✅*   |     ❌      |

\* Requires the USB opt-in below.

## Enabling USB flashing (local only)

Flashing needs the container to see the USB serial device. Edit
[devcontainer.json](devcontainer.json) and add (mirroring defoogi's own `start`
script):

```jsonc
"runArgs": ["--privileged", "-v", "/dev:/dev"],
```

Then **Dev Containers: Rebuild Container**. The `wario` user is already in the
`dialout` group, so `./build.sh -u` / `-m` can reach `/dev/ttyUSB*`.

> Device passthrough is a Linux/Docker-Engine feature. On Docker Desktop for
> macOS/Windows, USB passthrough to Linux containers is limited — flashing is
> most reliable on a Linux host. Either way you can always compile in the
> container and flash with a host-side tool if needed.

## Notes

- **PlatformIO cache:** ESP32 toolchains (several GB) are stored in a named
  Docker volume (`fujinet-platformio`) mounted at `/home/wario/.platformio`, so
  they persist across container rebuilds and don't pollute the source tree.
- **clang-format:** added on top of the base image so the `coding-standard.py`
  pre-commit hook works inside the container.
- **Image arch:** `fozztexx/defoogi:latest` ships both `amd64` and `arm64`, so
  Apple-Silicon machines run natively (no emulation).
