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
| 5           | `'4'`       | 800K GCR floppy      | `.moof`, `.dsk`/`.img` (400K or 800K sector image), `.image` (DiskCopy 4.2) |
| 6 – 8       | –           | unused               | mounting is refused |

The firmware refuses to mount a `.moof` anywhere but slot 5 and refuses a
DCD image outside slots 1–4, because the Pico protocol has no way to
address them.

### HD20 image formats

Both kinds of hard-disk image found in the wild work in slots 1–4:

* **Volume images** – a bare HFS (or MFS) volume, boot blocks in block 0
  and the master directory block in block 2. The whole file is the disk.
  This is what SavageTaylor calls a "volume" and what FloppyEmu uses.
* **Drive images** – a whole-disk image with an Apple Driver Descriptor
  Record (`ER`) in block 0 and an Apple partition map (`PM`) after it.
  The firmware finds the first `Apple_HFS` partition and presents only
  that partition to the Mac; the SCSI driver partitions are ignored
  because the HD20 protocol has no use for them.
* **DiskCopy 4.2** `.image` files – the 84-byte header is skipped.

At mount time the console shows which kind was detected and compares the
HFS header's idea of the volume size with the image size. A truncated
image (volume claims more blocks than the file holds) is logged with a
warning; the Mac will report such a disk as "damaged", which is correct.

A real HD20 is about 20 MB (38,965 blocks). Images up to 32 MB (65,535
blocks) are known to work on an SE/30; a 40 MB volume was rejected by
the Mac without reading a block, so keep HD20 images at or below 32 MB.

Mount a slot in write mode (mode "W" in the web UI) to let the Mac write.
The file on the TNFS server must be writable by the user tnfsd runs as.

Reference images that are known good with this firmware:
<https://www.savagetaylor.com/downloads/downloads-macintosh/>
(`320_32MB_volume.zip` and `320_32MB_drive.zip`).

### Floppy images

Slot 5 accepts flux-level MOOF images and plain sector images. A sector
image (409600 or 819200 bytes, or a DiskCopy 4.2 file) is run through the
GCR encoder in `lib/media/mac/macGCR.cpp` at mount time; all 80
cylinders end up in PSRAM (about 620 KB for 400K, 1.2 MB for 800K) and are
then streamed exactly like a MOOF. The encoder is verified by
`tests/mac_gcr_test.cpp`, which round-trips every sector through an
independent decoder; run it with a `.dsk`/`.img` as the argument to check
a specific image. The encoder follows the layout in
<https://github.com/lampmerchant/tashnotes/tree/main/macintosh/floppy>.

Sector images mounted read/write can be written by the Mac (verified on a
512Ke booting and running from a 400K MFS image, Finder writes included;
see `mac68k-floppy-write.md` for how it works). MOOF images and read-only
mounts are reported as write protected. Hard disks (slots 1-4) are
read/write.

The drive identifies itself from the disk: a 400K image makes it a
single-sided 400K drive (the only kind the 64K-ROM 128K/512K know), an
800K image a double-sided 800K drive.

#### Boot volumes

`tools/mac68k/make_hd20_volume.py` turns any HFS volume image (the 2 GB
SavageTaylor volumes, a 40 MB image) into a 32 MB HD20 volume: copies
the files, keeps the boot blocks, blesses the system folder. Volumes
must stay at or below 65,535 blocks (32 MB) or the Mac rejects them.
The mount code checks the blessing (MDB drFndrInfo[0]) and fixes it on
read/write mounts. On a 512Ke use System 5.1: System 6.0.8 does not run
in 512 KB (the Mac reports the System file as damaged).

### Booting from an HD20

With no floppy in slot 5 and a volume with a System in slot 1, a 512Ke,
Plus, SE or SE/30 boots from the emulated HD20 (verified on a 512Ke with
the 32 MB reference volume). The ROM's boot-time probe checks the device
with DCD states 5, 6 and 7 and then asserts HOST by going straight to
state 3; the Pico answers the handshake from any prior state for that
reason. A plain 512K (64K ROM) has no HD20 driver and needs a floppy that
carries the "Hard Disk 20" startup INIT.

Verified on an SE/30 with System 6.0.8: 400K MFS and 800K HFS sector
images and MOOFs boot, mount at the desktop and copy files.

### Booting from a floppy

The Mac behaves exactly as it would with a real external drive: **Restart
and Shut Down eject every floppy** (motor on, wait for ready, seek to the
middle cylinder, motor off, eject), so a disk that is mounted while the
Mac is running is gone by the time the ROM looks for a boot disk. To boot
from an image, either

* power the Mac off, mount the image from the web UI, then power on, or
* restart, and mount the image once the ROM is polling for a disk (the
  blinking "?" / the SCSI wait).

The ROM then reads the boot blocks, the catalog and the System file and
boots. For that to work the Pico has to look like a real Sony drive in
four places, all in `pico/mac/commands.c`:

* `!READY` stays high for 350 ms after motor on (spin-up) and while the
  head is settling after a step, until the ESP32 reports the new track
  loaded (`'S'`). The ROM verifies the cylinder in the first address
  field it reads after a seek and recalibrates when it sees the previous
  cylinder's headers.
* The "disk changed" status line (CA2:0 CA1:1 CA0:1 SEL:0) goes high on
  insertion and is cleared by the Mac's clear command (CA2:1 CA1:0 CA0:0
  SEL:1). Both the ROM and the System driver use it.
* The motor stops when `!ENBL` goes high, so a reset always spins up
  afresh.
* An eject command that no motor activity preceded is ignored: the bus
  floats while the Mac is switched off or on and can decode as an eject,
  which used to unmount the image before the ROM ever saw it.

Nothing in the ROM measures TACH; it only samples it as one status bit.

### Swapping HD20 images

Never change an HD20 slot while the Mac is running with that volume
mounted: the Mac caches the volume header and writes it back into
whatever image is in the slot by then, which then shows up as damaged.
Shut the Mac down first, or unmount the volume on the Mac.

### Swapping floppies

Always eject on the Mac first (drag the disk to the Trash), then mount
the next image from the web UI. Ejecting from the web UI while the Mac
still shows the volume leaves the two sides out of step, and the Mac's
later Trash-eject will unmount whatever was mounted next.

### Pico console

`pico_enable_stdio_usb` is on, so the Pico prints its debug output on its
own USB port (it enumerates as a serial device on the host). Connect it
when debugging the Mac side of the protocol. Typing `t` on that console
toggles the status trace: the Pico records which status line the Mac
selects (CA0-2/SEL), every phase command and every ESP32 event, and dumps
the timeline at each motor off and eject. It is off by default because a
full dump stalls the command loop. In the dump, each `+N` is how long the
*previous* line's state lasted, in microseconds.

`picotool load -f -x commands.uf2` reflashes a running Pico over USB
without touching BOOTSEL.

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

The Pico must run firmware built from this tree (or later). Older builds
wait forever for the Mac's LSTRB line when switching to floppy mode and
wedge if the Mac is hung or idle with LSTRB high; the ESP32 cannot reset
the Pico, so the symptom is "nothing happens until power cycle".

## Files

* `lib/bus/mac/mac.{h,cpp}` – the bus: UART link to the Pico, command dispatch
* `lib/bus/mac/mac_ll.{h,cpp}` – RMT bitstream output for floppy read data
* `lib/device/mac/floppy.{h,cpp}` – one disk slot (floppy or DCD depending on slot)
* `lib/device/mac/macFuji.{h,cpp}` – the Fuji device (thin subclass of `fujiDevice`)
* `lib/media/mac/` – MOOF and DCD image handling
* `include/pinmap/mac_rev0.h` – pin assignments (based on the FujiApple Rev0 pinmap)
* `pico/mac/` – the Pico firmware and PIO programs
