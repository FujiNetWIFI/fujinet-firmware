# boards

PlatformIO board definitions for FujiNet hardware, plus two JSON files that only
[mcuconfig/](../mcuconfig/) reads.

## Layout
| File | Defines |
|---|---|
| `fujinet-v1.json`, `fujinet-v1-8mb.json`, `fujinet-v1-4mb.json` | ESP32 FujiNet v1 boards with 16, 8 and 4 MB flash |
| `fujinet-cx16.json` | ESP32 with 8 MB flash for the Commander X16 build |
| `esp32-s3-wroom-1-n16r8.json`, `esp32-s3-wroom-1-n8r8.json` | ESP32-S3 WROOM-1 modules; both add `sdkconfig.defaults.psram_octal` through `cmake_extra_args` |
| `esp32-s3-xdrive-n4r2.json` | ESP32-S3 with 4 MB flash and the partition table without an update slot |
| `fujinet-esp32s3.json`, `fujinet-esp32s3-8mb.json` | Generic ESP32-S3 definitions |
| `buses.json` | Bus name to the pin signal names that bus needs |
| `capabilities.json` | Capability group (SD card, buttons, LEDs, UARTs and so on) to its signal names |

## How it fits
- A board ini in [build-platforms/](../build-platforms/) names one of these in its `board` key;
  PlatformIO finds them here because `boards/` is its default custom-board directory.
- Each JSON sets the flash size and points at one of the root `fujinet_partitions_*.csv` tables.
- `buses.json` and `capabilities.json` are read by `mcuconfig/mcuconfig.py` to validate pinmaps.
  Nothing in the firmware or the PlatformIO build reads them.

## Build
Consumed by every ESP32 build through the generated `platformio-generated.ini`. The PC build does
not use this directory.

## Notes
`fujinet-v1-4mb.json`, `fujinet-esp32s3.json` and `fujinet-esp32s3-8mb.json` are not selected by
any board ini.
