# lib/device/comlynx

The Atari Lynx device set for `BUILD_LYNX`.

## How it fits
- Every class derives the `virtualDevice` of [lib/bus/comlynx/](../../bus/comlynx/). Images come
  from [lib/media/lynx/](../../media/lynx/).
- There is no clock device.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_LYNX`. PC: `FUJINET_TARGET=LYNX`
lists every file and adds `components/lz4` for the disk device.

## Notes
- `modem.h-na` and `modem.cpp-na` are a modem device the `-na` suffix keeps out of the build.
