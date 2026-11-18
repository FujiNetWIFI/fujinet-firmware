# .github/workflows

GitHub Actions for the ESP32 firmware, the FujiNet-PC host build and the cartridge firmware.
Firmware CI is build-only; the PC workflow is the only one that runs tests.

## How it fits
- The Linux jobs in `autobuild.yml`, `nightly.yml`, `release.yml` and `pico-carts.yml` run in an
  `ubuntu:24.04` container on the self-hosted `fujinet-builder` runner. Each starts with an
  "Install base tools" step because the bare image has no git, `gh` or compiler.
- The ESP32 jobs keep PlatformIO's packages in the host Docker volume `fujinet-ci-cache`, mounted at `/ci-cache`. Pull-request jobs mount it
  read-only, so only push, schedule and release jobs can add to it and a PR cannot plant a toolchain that a later build would use.
- Containers reach a caching apt mirror on the runner host (`host.docker.internal:3142`, backed by mirrors.edge.kernel.org) and
  fall back to that mirror directly when it is down; the default `archive.ubuntu.com` often times out from the runner.
- `tahoe` is an Intel macOS VM with no Homebrew, thrown away after each job; `build-fujinet-pc.yml` builds its own
  dependencies there. Only the arm64 build is made, because an Intel Mac binary is no longer worth the build time.
- Every ESP32 job is `build.sh` with one of the `platformio.release-*.ini` overlays as its
  local ini, so the overlays are the
  authoritative list of release boards. [build-platforms/](../../build-platforms/) holds the board
  inis they select.
- `.github/PULL_REQUEST_TEMPLATE.md`, one level up, is the pull request prompt sheet.

## Notes
- The macOS build in `build-fujinet-pc.yml` is a compile check only: its tests cannot run on the
  Intel `tahoe` runner.
- `preprelease.sh` writes the per-platform release JSON that the flasher reads.
- `coding-standard-requirements.txt` lists the dependencies of `coding-standard.py`; no workflow
  installs them.
- `autobuild.yml` and `build-fujinet-pc.yml` skip a pull request that changes only Markdown;
  `docs-only.yml` then reports the base branch's required checks as passed.
- `nightly.yml` runs only while the `RUN_NIGHTLY` repository variable is true.
