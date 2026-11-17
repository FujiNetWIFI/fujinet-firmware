# lib/device/iwm

The Apple II device set for `BUILD_APPLE`: SmartPort devices plus Disk II emulation.

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/iwm/](../../bus/iwm/), implements the
  SmartPort verbs it supports and supplies `create_dib_reply_packet()` and
  `create_status_reply_packet()`. Images come from [lib/media/apple/](../../media/apple/).
- `iwmFuji::setup()` registers itself, the networks, `platformClock`, CP/M and the disks.
  `iwmClock` maps writes to `iwm_ctrl()` and reads to `iwm_status()`.
- `src/main.cpp` adds the modem and printer before calling `SYSTEM_BUS.theFuji->setup()`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_APPLE`. PC: `FUJINET_TARGET=APPLE`
lists every file, but `disk2.cpp` is wrapped in `ESP_PLATFORM`, so Disk II exists only on hardware.
