# FujiNet for the Macintosh 68k (floppy port)

The Mac 68k FujiNet board ("fujimac-rev0") pairs an ESP32-WROVER-E with a
Raspberry Pi Pico (RP2040). The Pico sits on the DB-19 floppy port and
emulates the drive-side protocols in PIO state machines:

* an 800K GCR floppy (Macintosh Control Interface, MCI), and
* up to four HD20-style hard disks (Directly Connected Disk, DCD).

The ESP32 provides the disk images (SD card, TNFS, etc.), the web UI, and
WiFi. The two MCUs talk over a 2 Mbaud UART with a one-character command
protocol; see the comment block at the top of `lib/bus/mac/mac.h`. Floppy
read data is streamed from the ESP32's RMT peripheral on `SP_WRDATA`, not
over the UART.

## Disk slots

| Web UI slot | Pico drive | Emulates | Image types |
|-------------|------------|----------|-------------|
| 1 – 4       | `'0'`–`'3'` | HD20 (DCD) hard disk | `.dsk` (raw 512-byte blocks), `.image` (DiskCopy 4.2) |
| 5           | `'4'`       | 800K GCR floppy      | `.moof` |
| 6 – 8       | –           | unused               | mounting is refused |

The firmware refuses to mount a `.moof` anywhere but slot 5 and refuses a
DCD image outside slots 1–4, because the Pico protocol has no way to
address them.

The Mac has no CONFIG program, so everything is driven from the web UI.
Either press **Mount all slots** in the web UI after boot, or disable
"Boot CONFIG" in the web UI's general settings so the firmware mounts the
configured slots automatically at startup.

## Building the ESP32 firmware

```sh
./build.sh -ys fujimac-rev0          # writes platformio.local.ini
# then add the serial port to platformio.local.ini:
#   [env]
#   upload_port = /dev/cu.usbserial-XXXX
#   monitor_port = /dev/cu.usbserial-XXXX
./build.sh -cb                       # clean + build
./build.sh -u                        # flash firmware
./build.sh -f                        # flash the LittleFS image (web UI files)
./build.sh -m                        # serial monitor
```

The ESP32 enumerates through a Silicon Labs CP2102N, so on macOS it shows
up as `/dev/cu.usbserial-XXXX`.

## Building the Pico firmware

Requirements: the Raspberry Pi Pico SDK (2.x works), CMake, Ninja, and an
`arm-none-eabi` GCC toolchain.

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
export PATH=/path/to/arm-gnu-toolchain/bin:$PATH
cd pico/mac
mkdir -p build && cd build
cmake -G Ninja -DPICO_BOARD=pico ..
ninja
```

This produces `commands.uf2`. To flash it, hold BOOTSEL on the Pico while
plugging in its USB port, then copy `commands.uf2` onto the `RPI-RP2`
volume that appears (or use `picotool load commands.uf2`).

Note: newer versions of `pioasm` reserve the word `next`, so the DCD
command program uses the label `changed` instead.

## Files

* `lib/bus/mac/mac.{h,cpp}` – the bus: UART link to the Pico, command dispatch
* `lib/bus/mac/mac_ll.{h,cpp}` – RMT bitstream output for floppy read data
* `lib/device/mac/floppy.{h,cpp}` – one disk slot (floppy or DCD depending on slot)
* `lib/device/mac/macFuji.{h,cpp}` – the Fuji device (thin subclass of `fujiDevice`)
* `lib/media/mac/` – MOOF and DCD image handling
* `include/pinmap/mac_rev0.h` – pin assignments (based on the FujiApple Rev0 pinmap)
* `pico/mac/` – the Pico firmware and PIO programs
