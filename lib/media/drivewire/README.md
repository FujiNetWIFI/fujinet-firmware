# lib/media/drivewire

CoCo and Dragon disk, cartridge and cassette image formats for `BUILD_COCO`.

## How it fits
- `discover_mediatype()` maps DSK, MRM and RMM, VDK, ROM and CCC, and CAS by extension;
  `drivewireDisk` in [lib/device/drivewire/](../../device/drivewire/) picks the subclass, or for
  ROM and CCC opens a `CoCoCartROM` from [lib/media/rom/](../rom/) through `ROMFactory`. MRM and
  RMM are PMD 85 ROM modules, so they open a `PMD85ROM` there instead of a disk class.
- Included by [lib/media/media.h](../media.h) under `BUILD_COCO`.

## Build
ESP: globbed by `src/CMakeLists.txt`, compiled only under `BUILD_COCO`. PC: `FUJINET_TARGET=COCO`
lists every file.
