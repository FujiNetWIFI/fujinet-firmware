# Intellivision FujiNet bridge

RP2040/RP2350 firmware that lets Intellivision programs talk to FujiNet. The cartridge
firmware (`firmware/`, see `PROVENANCE.md` for license status) is a fork of Gennaro
Tortone's **Minty** — GPLv3, unlike the unlicensed PiRTO II fork this branch used
previously — extended so it also bridges the Intellivision's CP-1610 bus to an ESP32-S3
running fujinet-firmware's `fujiversal-rs232` build over USB CDC.

```
Intellivision --CP-1610 bus--> RP2040/RP2350 (Minty fork) --USB CDC--> ESP32-S3 fujiversal-rs232
   CONFIG           PEEK/POKE $9C00-$9F3F mailbox         device->host      FujiBus/SLIP
```

The ESP32-S3 side (`lib/hardware/ACMChannel.cpp` in the main fujinet-firmware tree) is the
USB **host**; the RP2040/RP2350 stays a USB **device** — no PIO-USB host stack required.

## Why the mailbox moved from $9800 to $9C00

The original PiRTO II-based prototype put the mailbox at `$9800-$9F3F`, inside a window
($8000-$9FFF) that firmware simply mapped as plain RAM. Minty's JLP (Jump-LP, an
accelerator/expanded-RAM peripheral some Intellivision homebrews use) emulation claims that
*same* `$8000-$9FFF` window for JLP RAM — so the two need to coexist. The mailbox now lives
at the *top* of the window, `$9C00-$9F3F` (832 words: a 64-word header, a 256-byte TX
payload, and a 512-byte RX payload — shrunk from 1536, since nothing FujiNet CONFIG sends
needs more), leaving `$8000-$9BFF` (7168 of 8192 words, ~87%) free for JLP. See
`firmware/include/fuji_mailbox.h` for the exact layout and `firmware/src/cartridge.c` for
how the two windows share the same RAM-claim branch in the bus loop at effectively zero
extra cost in the timing-critical path.

## Layout

- `firmware/` — the forked Minty cartridge firmware (RP2040/RP2350, pico-sdk 2.2.0). See
  its own `README.md` for Minty's general features; the FujiNet-specific additions are:
  - `boards/fujicard.{cmake,h}` — new board, derived from `pintycard` (RP2354A, LittleFS,
    no SD card), with the USB CDC device stack forced on in Release builds (the CDC link to
    the ESP32-S3 is the whole point) and `CONFIG_FUJINET` enabled.
  - `src/fujinet.c`/`include/fujinet.h` — the mailbox service (`fuji_mailbox_service()`) and
    the ROM-boot receiver (`dbc_inbound_handler()`, `feed_rom_byte()`,
    `apply_boot_mapping()`) that lets the ESP32 push a ROM (+ optional `.cfg` sibling) over
    the same USB-CDC link a MOUNT_IMAGE mailbox transaction is using.
  - `src/fujiboot.c`/`include/fujiboot.h` — `RunFujiConfig()`, which boots the Intellivision
    straight into FujiNet CONFIG. Minty's own SD/flash launcher (`src/launcher.c` and
    friends) is **deleted** in this fork — there is no menu, no chord, no fallback; CONFIG
    is the only boot ROM.
  - `include/fujiconfigrom.h` — the compiled CONFIG program, baked in as a byte array (see
    "Building" below for how to regenerate it).
  - `src/fujibus.c`/`include/fujibus.h` — plain-C port of `lib/bus/rs232/FujiBusPacket.cpp`'s
    wire format. No hardware dependency; builds and unit-tests on a desktop (see
    `firmware/host_test/`).
  - `src/fujibus_usb.c`/`include/fujibus_usb.h` — TinyUSB CDC transport on top of the codec
    above (`fujibus_transact()`).
  - `include/fuji_mailbox.h` — the `$9C00-$9F3F` mailbox address map. Single source of truth
    for the RP2040/RP2350 side; `fujinet-config/intv/fujinet.bas` hardcodes the same offsets
    by hand (IntyBASIC has no `#include`).
- `PROVENANCE.md` — licensing status of the vendored Minty firmware (resolved: GPLv3) and
  the still-unresolved PCB question. Read this before doing anything with `firmware/`
  beyond local development.

The boot ROM itself — FujiNet CONFIG (WiFi setup, host slots, directory browsing, boot a
selected ROM) — lives in a separate repository, `~/Workspace/fujinet-config/intv/`, not in
this tree. See its own README for build/test instructions.

## Status

Working end-to-end on real hardware on the prior PiRTO II-based prototype (Intellivision +
cart + ESP32-S3, verified reliable across repeated console resets); this port carries that
protocol and the same ESP32-side fixes forward onto Minty, but has not yet itself been
verified on real silicon — see the plan/PR this was implemented against for the verification
checklist. If the mailbox regresses after a console reset, re-check that
`fujinet-config/intv/fujinet.bas` still derives `FN_SEQ` from `PEEK(FN_ACKSEQ)+1`, not a
local counter — a console reset zeroes IntyBASIC variables but not the RP2040/RP2350, so a
locally-incrementing counter recomputes the same value forever and silently never triggers a
second transaction after the first boot.

## Building

RP2040/RP2350 firmware:
```sh
cd firmware
cmake -B build -DPICO_BOARD=fujicard -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja -C build
# flash the resulting Minty_fujicard.uf2 to the board in BOOTSEL mode
```

From the repo root, `./build_pico.py fujiversal-intv` (or `./build.sh -P -e fujiversal-intv`)
does exactly this -- driven by the `[fujinet] pico_*` keys in
`build-platforms/platformio-fujiversal-intv.ini` -- and is also what runs automatically as
a `pre:` extra_script (`build_pico.py`) whenever the `fujiversal-intv` PlatformIO env is
built, generating `lib/hardware/fn_pico_blob_data.cpp` from the result. The manual steps
above remain useful for iterating on the RP2040/RP2350 firmware directly without going
through PlatformIO at all.

`fujibus.c`'s codec, standalone (no RP2040 toolchain needed):
```sh
cd firmware/host_test
gcc -Wall -Wextra -I.. -I../include -o /tmp/test_fujibus test_fujibus.c ../src/fujibus.c && /tmp/test_fujibus
```

FujiNet CONFIG boot ROM (from `~/Workspace/fujinet-config/intv/`):
```sh
cd ~/Workspace/fujinet-config/intv
make rom.h
cp config_rom.h /path/to/fujinet-firmware/pico/intellivision/firmware/include/fujiconfigrom.h
```
then rebuild the RP2040/RP2350 firmware as above. See that repository's own README for how
to test CONFIG against jzIntv (the FujiNet-patched Intellivision emulator) without hardware,
including a `--fujinet-bootdump` mode that validates the ESP32's ROM-push path byte-for-byte
against a source ROM with no cart attached at all.

-Thom
