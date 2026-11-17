# lib/device/sio

The Atari device set for `BUILD_ATARI`: every SIO device the firmware presents to the Atari,
built on the shared bases in [lib/device/](../).

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/sio/](../../bus/sio/) and implements
  `sio_status()` and `sio_process()`. Disk images come from [lib/media/atari/](../../media/atari/);
  printers use [lib/printer-emulator/](../../printer-emulator/).
- `src/main.cpp` adds the Fuji, clock, PCLink, printer, modem and voice devices;
  `sioFuji::setup()` adds the rest.

## Build
ESP: globbed by `src/CMakeLists.txt`; compiled only under `BUILD_ATARI`, except `cassetteTrail.cpp`
and `cassetteFSKRead.cpp`, which have no guard; `cassetteFSK.cpp` and `cassetteFSKLoader.cpp` also need
`ESP_PLATFORM`. PC: `FUJINET_TARGET=ATARI` lists every file; the cassette trail, rewind request, FSK
read recovery and DSTATS logic have tests under [tests/](../../../tests/).
