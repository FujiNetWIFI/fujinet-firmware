# lib/device/cx16_i2c

The Commander X16 device set for `BUILD_CX16`. Legacy: fails to build (#1658).

## How it fits
- Devices implement the legacy `process(commanddata, checksum)` of
  [lib/bus/cx16_i2c/](../../bus/cx16_i2c/). No network device is wired in; `lib/device/device.h`
  comments it out. Media comes from [lib/media/cx16/](../../media/cx16/).

## Build
ESP only, `BUILD_CX16`; not compiled on PC.
