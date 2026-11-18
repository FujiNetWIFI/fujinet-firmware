# lib/display

Animation for an addressable-LED display driven over SPI: bus activity, transfer progress and an
idle pattern. ESP only, and only when `ENABLE_DISPLAY` is defined.

## How it fits
- Started and serviced from `src/main.cpp`; the IEC drive in [lib/device/iec/](../device/iec/) and
  the console commands in [lib/console/](../console/) drive the animations.
- Pins come from `include/pinmap.h`. Meatloaf heritage.

## Build
ESP: globbed, but the whole source is under `#ifdef ENABLE_DISPLAY`, which no board ini defines
(it appears only as a commented-out flag in `platformio-sample.ini`). PC: not compiled.
