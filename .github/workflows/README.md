# .github/workflows

GitHub Actions for the ESP32 firmware, the FujiNet-PC host build and the cartridge firmware.
Firmware CI is build-only; the PC workflow is the only one that runs tests.

## Layout
| File | Runs on | Does |
|---|---|---|
| `autobuild.yml` | push and pull request, not tags or the release branch | `build.sh -y` then `build.sh -y -z` for every target in its matrix, with the matching `platformio.release-*.ini` as the local ini; the Fujiversal targets install the ARM toolchain from apt, and `build.sh` installs the pinned pico-sdk |
| `nightly.yml` | a daily schedule, only while the `RUN_NIGHTLY` repository variable is true | The same build, renamed to a nightly zip and published to the `nightly` release |
| `release.yml` | a `v*` tag or manual dispatch | Builds the release zips, runs `preprelease.sh`, uploads the GitHub release and notifies fujinet.online |
| `build-fujinet-pc.yml` | push, pull request, a nightly schedule, and manual dispatch with an optional release draft | `./build.sh -y -p <TARGET>` for ADAM, APPLE, ATARI, COCO, IEC, LYNX and RS232 on Ubuntu (container on the self-hosted runner), macOS Intel and macOS ARM (both on the self-hosted `tahoe` runner, MbedTLS and OpenSSL built from source) and Windows (GitHub-hosted); that invocation runs `ctest`, except for the ARM build, which cannot run on the Intel runner (`FUJINET_SKIP_TESTS=1`) |
| `pico-carts.yml` | changes under `pico/`, to `build_pico.py`, to the Fujiversal board inis or to `platformio.common.ini` | Host tests for the cartridge code, the 8048 and Z80 console clients, the pico-sdk cartridge builds, and `build_pico.py` in no-generate mode for the Fujiversal boards |
| `platformio.release-*.ini` | | Per-target local-ini overlays passed with `build.sh -l`; most set `build_board` and add `build_firmwarezip.py`, a few define a complete board section |
| `preprelease.sh` | | Writes the per-platform release JSON that the flasher reads, from a built zip |
| `coding-standard-requirements.txt` | | Dependencies for `coding-standard.py`; no workflow installs them |

## How it fits
- The Linux jobs in `autobuild.yml`, `nightly.yml`, `release.yml` and `pico-carts.yml` run in an
  `ubuntu:24.04` container on the self-hosted `fujinet-builder` runner. Each starts with an
  "Install base tools" step because the bare image has no git, `gh` or compiler.
- Containers reach a caching apt mirror on the runner host (`host.docker.internal:3142`, backed by mirrors.edge.kernel.org) and
  fall back to that mirror directly when it is down; the default `archive.ubuntu.com` often times out from the runner.
- `tahoe` is an Intel macOS VM with no Homebrew, thrown away after each job; `build-fujinet-pc.yml` builds its own
  dependencies there.
- Every ESP32 job is `build.sh` with one of the overlays as its local ini, so the overlays are the
  authoritative list of release boards. [build-platforms/](../../build-platforms/) holds the board
  inis they select.
- `.github/PULL_REQUEST_TEMPLATE.md`, one level up, is the pull request prompt sheet.
