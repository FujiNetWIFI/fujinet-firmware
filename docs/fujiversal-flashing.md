# Flashing a Fujiversal board: one image, one command, two chips

A Fujiversal board is two microcontrollers. An ESP32-S3 runs FujiNet proper, and an
RP2040/RP2350/RP2354 sits in the retro machine's cartridge slot pretending to be a ROM and
relaying bus traffic. They are joined by USB, with the ESP32-S3 as the host. Only the
ESP32-S3's UART is brought out for the user to plug into a PC.

The boards using this design today:

| Board env | Machine | Cartridge firmware | Cartridge board | Chip |
|---|---|---|---|---|
| `fujiversal-intv` | Intellivision | `pico/intellivision/firmware` (in tree) | `fujicard` | RP2354A |
| `fujiversal-drivewire` | Tandy CoCo | `pico/fujiversal` (submodule) | `coco_proto_260402` | RP2350B |
| `fujiversal-msx` | MSX | `pico/fujiversal` (submodule) | `msx_proto_260402` | RP2350B |

The cartridge firmware is **built alongside the ESP32 firmware and embedded inside
`firmware.bin`** as read-only data. Nothing about the flashing tool, the firmware zip's
layout, or the partition table changes: the FujiNet-Flasher writes the ESP32 over the UART
exactly as it does for every other board, and the ESP32 then flashes the cartridge itself.

## What happens, in order

```
FujiNet-Flasher                ESP32-S3                      cartridge MCU
     |                            |                                |
     | erase chip, write 5 files  |                                |
     |--------------------------->|                                |
     | hard reset, stream UART0   |                                |
     |--------------------------->|                                |
     |                        boot: NVS is empty after the erase    |
     |                            |  ask it to enter BOOTSEL        |
     |                            |------------------------------->|
     |                            |  erase / write / verify        |
     |                            |<------------------------------>|
     |                            |  reboot into the new firmware  |
     |                            |------------------------------->|
     |  PICOFW: ... OK            |                                |
     |<---------------------------|                                |
     |                        bus starts, cartridge link comes up   |
```

The user sees this on the same serial port they flashed over, as `PICOFW:` lines. The
flasher points them out and asks that the board stay powered until one says `OK` or
`up to date`.

## When the cartridge is rewritten

Only when it needs to be. After a successful flash the ESP32 records the image's sha256 in
NVS (namespace `picofw`, one key per image name). On each boot it compares:

- **A cartridge already in BOOTSEL** is always flashed, whatever NVS says. It is either
  blank or was deliberately put there, and either way this image is what it should get.
- **A running cartridge whose sha256 matches** is left completely alone. No reset, no
  interruption, and the boot costs well under a second.
- **A running cartridge whose sha256 differs** is asked to reboot into BOOTSEL and flashed.

Flashing a FujiNet erases the whole chip including NVS, so **a flasher run always reflashes
the cartridge exactly once**, and every boot after that is the quick path.

A consequence worth knowing: if you flash a cartridge by hand with a development build, the
ESP32 will leave it alone, because NVS still says the embedded image is what is on there.
Force a reflash by reflashing the ESP32, by putting the cartridge in BOOTSEL, or from code
via `fnPicoUpdater.forgetFlashedImages()`.

## Getting into BOOTSEL

In order of preference:

1. **The 1200-baud request.** Setting the cartridge's USB serial line to 1200 baud is the
   convention every pico tool uses to mean "reboot into the bootloader". This is the normal
   path and needs no hardware and no help from the retro machine. It requires the cartridge
   firmware to implement it, which means linking `pico_usb_reset` **and** defining
   `PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1` — the SDK turns it off by default for a project
   that links `tinyusb_device` directly, so linking the library alone silently does nothing.
   Check a build with `arm-none-eabi-nm <elf> | grep line_coding_cb` before trusting it.
2. **The RUN and BOOTSEL lines**, where the board wires them to ESP32 GPIOs
   (`PIN_RP2040_RUN` / `PIN_RP2040_BOOTSEL` — today only `fujiversal-intv`). The ESP32 holds
   BOOTSEL low across a reset pulse on RUN. This works even when the cartridge firmware is
   bricked or hung, which is what makes a board with no external cartridge USB port safe.
3. **The cartridge's own BOOTSEL button**, on the separate-board designs (CoCo, MSX) that
   have no GPIO wiring. Hold it while powering up, and the next ESP32 boot finds it in
   BOOTSEL and flashes it.
4. **The mailbox doorbell** on the Intellivision, where the console itself writes a magic
   byte (`FUJI_MB_BOOTSEL_DOORBELL` in `pico/intellivision/firmware/include/fuji_mailbox.h`).
   Needs working firmware and an Intellivision, so it is the least useful for recovery.

A failed flash is not a brick. The cartridge is left in BOOTSEL, the sha256 is not recorded,
and the ESP32 carries on booting — so the next boot finds it and tries again.

## Reading the log

```
PICOFW: image msx_fw 109868 bytes sha256 93532f4b chip rp2350 base 0x10000000 limit 0x11000000
PICOFW: device 2E8A:000F is in BOOTSEL
PICOFW: flashing msx_fw (109868 bytes, 27 sectors from 0x10000000)
PICOFW: write 10% ... 100%
PICOFW: verify 10% ... 100%
PICOFW: OK msx_fw flashed and verified (109868 bytes in 4.2s); rebooting the companion
PICOFW: companion is up
```

Other lines you may see:

| Line | Meaning |
|---|---|
| `up to date (... ); no reflash needed` | The quick path. Nothing was touched. |
| `no companion attached within 3000ms; continuing boot` | Nothing on the USB port. Normal on a bare ESP32 module. |
| `companion is running <sha>, image is <sha>; asking for BOOTSEL` | A reflash is starting. |
| `the companion did not enter BOOTSEL` | Neither the 1200-baud request nor the pins worked. Use the BOOTSEL button. |
| `FAIL <stage> at <addr>: ... (bootrom status BAD_ALIGNMENT)` | The flash itself failed, with the bootrom's own reason. Retried next boot. |
| `SKIP no companion image embedded in this build` | Built with `FUJINET_SKIP_PICO`, or a board with no cartridge. |
| `WARN the companion has not reappeared yet` | It was flashed but has not re-enumerated. The bus will keep waiting. |

## Adding another Fujiversal board

Four places, no code:

1. **`build-platforms/platformio-<board>.ini`** — in `[fujinet]`: `build_board`,
   `platform_name` (needed whenever boards share a `build_platform`, or their zips collide),
   and the `pico_*` keys (`pico_src`, `pico_chip`, `pico_artifacts`, plus
   `pico_flash_limit` if the cartridge keeps anything else in flash, and `pico_prebuild` if
   its build needs generated inputs). In `[env:<board>]`: the three
   `-D CONFIG_USB_{HOST,CDC_ACM_HOST,PICOBOOT_HOST}_ENABLED=1` defines. The full key table
   is a comment block in `platformio-ini-files/platformio.common.ini`.
2. **`include/pinmap/<board>.h`** plus an include in `include/pinmap.h`. Copy the closest
   sibling. Set `FN_USB_EXPECTED_VID` to the cartridge's vendor ID, and add
   `PIN_RP2040_RUN`/`PIN_RP2040_BOOTSEL` only if the board really wires them.
3. **`data/webui/config/<board>.yaml`** if the board needs its own web UI config.
4. **CI**: a `.github/workflows/platformio.release-<NAME>.ini` overlay, the target added to
   the `autobuild`/`nightly`/`release` matrices, and a `pico: true` entry in each `include:`
   block so the job installs the ARM toolchain.

`build_pico.py` refuses to embed an image for a board that has not set
`CONFIG_USB_PICOBOOT_HOST_ENABLED`, and refuses an image that would be written past
`pico_flash_limit`, so the two commonest mistakes fail the build rather than the hardware.

## Building and testing locally

```sh
# companion firmware only, fast, no ESP-IDF build
./build.sh -P -s fujiversal-msx
python3 build_pico.py fujiversal-msx --print-config     # what the ini resolved to
python3 build_pico.py fujiversal-msx --dry-run          # the commands it would run

# the whole thing
./build.sh -s fujiversal-msx -b

# a release zip, and what it claims to carry
./build.sh -z -s fujiversal-msx
unzip -p firmware/fujinet-FUJIVERSAL-MSX-*.zip release.json | jq .companion
```

Set `FUJINET_SKIP_PICO=1` to build the ESP32 side alone, on a machine with no ARM
toolchain. The firmware still builds and runs; it simply carries no cartridge image and
logs `SKIP`. `build-platforms/build-all.sh` sets this for itself when no toolchain is
present.

After a fresh clone, the CoCo and MSX cartridge source needs fetching once:

```sh
git submodule update --init pico/fujiversal
```

`build_pico.py` says exactly this if you forget.
