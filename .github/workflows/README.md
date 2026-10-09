# .github/workflows

GitHub Actions for the ESP32 firmware, the FujiNet-PC host build and the cartridge firmware.
Firmware CI is build-only; the PC workflow is the only one that runs tests.

## Layout
| File | Runs on | Does |
|---|---|---|
| `autobuild.yml` | push and pull request, not tags or the release branch | `build.sh -y` then `build.sh -y -z` for every target in its matrix, with the matching `platformio.release-*.ini` as the local ini; the Fujiversal targets install the ARM toolchain from apt, and `build.sh` installs the pinned pico-sdk |
| `nightly.yml` | a daily schedule, only while the `RUN_NIGHTLY` repository variable is true | The same build, renamed to a nightly zip and published to the `nightly` release |
| `release.yml` | a `v*` tag or manual dispatch | Builds the release zips, runs `preprelease.sh`, uploads the GitHub release and notifies fujinet.online |
| `build-fujinet-pc.yml` | push, pull request, a nightly schedule, and manual dispatch with an optional release draft | `./build.sh -y -p <TARGET>` for ADAM, APPLE, ATARI, COCO, LYNX and RS232 across Ubuntu, macOS and Windows runners; that invocation runs `ctest` |
| `pico-carts.yml` | changes under `pico/`, to `build_pico.py`, to the Fujiversal board inis or to `platformio.common.ini` | Host tests for the cartridge code, the 8048 and Z80 console clients, the pico-sdk cartridge builds, and `build_pico.py` in no-generate mode for the Fujiversal boards |
| `platformio.release-*.ini` | | Per-target local-ini overlays passed with `build.sh -l`; most set `build_board` and add `build_firmwarezip.py`, a few define a complete board section |
| `preprelease.sh` | | Writes the per-platform release JSON that the flasher reads, from a built zip |
| `coding-standard-requirements.txt` | | Dependencies for `coding-standard.py`; no workflow installs them |

## How it fits
- Every ESP32 job is `build.sh` with one of the overlays as its local ini, so the overlays are the
  authoritative list of release boards. [build-platforms/](../../build-platforms/) holds the board
  inis they select.
- `.github/PULL_REQUEST_TEMPLATE.md`, one level up, is the pull request prompt sheet.
