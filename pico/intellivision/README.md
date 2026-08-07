# Intellivision FujiNet bridge

RP2040 firmware that lets Intellivision programs talk to FujiNet. The RP2040 is a fork of
Andrea Ottaviani's **PiRTO II** flash multicart (`firmware/`, see `PROVENANCE.md` for
license status — important, read before redistributing anything in here), extended so it
also bridges the Intellivision's CP-1610 bus to an ESP32-S3 running fujinet-firmware's
`fujiversal-rs232` build over USB CDC.

```
Intellivision --CP-1610 bus--> RP2040 (PiRTO II fork) --USB CDC--> ESP32-S3 fujiversal-rs232
   IntyBASIC        PEEK/POKE $9800-$9FFF mailbox         device->host      FujiBus/SLIP
```

The ESP32-S3 side (`lib/hardware/ACMChannel.cpp` in the main fujinet-firmware tree) is the
USB **host**; the RP2040 stays a USB **device**, same as stock PiRTO II — no PIO-USB host
stack required.

## Layout

- `firmware/` — the forked PiRTO II cartridge firmware (RP2040, pico-sdk).
  - `inty_cart.c` — CP-1610 bus emulation (core1, untouched hot loop) and the FujiNet
    command loop / mailbox service (core0, added).
  - `fujibus.c`/`.h` — plain-C port of `lib/bus/rs232/FujiBusPacket.cpp`'s wire format.
    No hardware dependency; builds and unit-tests on a desktop (see `firmware/host_test/`).
  - `fujibus_usb.c`/`.h` — TinyUSB CDC transport on top of the codec above
    (`fujibus_transact()`).
  - `fuji_mailbox.h` — the `$9800-$9FFF` mailbox address map. Single source of truth for
    the RP2040 side; `intv/fujitest.bas` hardcodes the same offsets by hand (IntyBASIC has
    no `#include`).
  - `rom.h` — the boot ROM baked into the cartridge (currently `intv/fujitest.bin`, so the
    console boots straight into the demo with no menu, no filesystem, no SD card).
- `intv/` — IntyBASIC test program(s) that exercise the FujiNet mailbox.
- `cart/` — original `booted.bas` scaffolding kept from the pre-fork bring-up lab, useful
  as a minimal reference for the `.bas` -> `.asm` -> `.bin`/`.cfg` toolchain.
- `PROVENANCE.md` — licensing status of the vendored PiRTO II / A8PicoCart code. Read this
  before doing anything with `firmware/` beyond local development.

## Status

Working end-to-end on real hardware (Intellivision + PiRTO II cart + ESP32-S3, verified
reliable across repeated console resets). Getting there required several real fixes beyond
the RP2040 side of this directory, in particular a reconnect bug in the shared
`lib/hardware/ACMChannel.cpp` (it never listened for a new device again after the first
successful USB connection). See the git history and commit messages for the full list —
several of these were the kind of bug that only reproduces on real silicon, never in
emulation, so if this regresses, re-read the commits before assuming it's something new.

## End-to-end demo

`intv/fujitest.bas` pokes a `GET_ADAPTER_CONFIG_EXTENDED` request into the mailbox, polls
for the RP2040's reply (bounded by a frame-count timeout so a missing cart/ESP32 fails
visibly instead of hanging), and prints the resulting SSID / firmware version / IP address
to the TV screen.

The mailbox's sequence number (`FN_SEQ`) **must** be derived from the RP2040's own
persisted `FN_ACKSEQ` (`seq=peek(FN_ACKSEQ)+1`), not from a local IntyBASIC variable. A
console reset re-zeroes every IntyBASIC variable but does not reset the RP2040, so a
locally-incrementing counter recomputes the same value on every reset and silently never
triggers a new transaction after the first one.

## Building

RP2040 firmware:
```sh
cd firmware && mkdir -p build && cd build
PICO_SDK_PATH=/usr/share/pico-sdk cmake -G Ninja ..
ninja
```

`fujibus.c`'s codec, standalone (no RP2040 toolchain needed):
```sh
cd firmware/host_test
gcc -Wall -Wextra -I.. -o /tmp/test_fujibus test_fujibus.c ../fujibus.c && /tmp/test_fujibus
```

IntyBASIC test ROM:
```sh
cd intv
intybasic fujitest.bas fujitest.asm /home/thomc/Workspace/IntyBASIC/intybasic/
as1600 -o fujitest.bin fujitest.asm   # also emits fujitest.cfg
as1600 -o fujitest.rom fujitest.asm   # Intellicart format, for jzIntv
```

Smoke-test the ROM in jzIntv without any hardware attached (there's obviously no RP2040
mailbox to answer it, so this only exercises the boot/timeout path -- it prints
`NO CARTRIDGE MAILBOX` and stops, which is the correct behavior):
```sh
jzintv fujitest.rom
```
To exercise the success path (SSID/version/IP display) without hardware, use jzIntv's
debugger (`-d --script=path`) to poke fake mailbox contents before running. A script that
pre-satisfies the mailbox handshake and fakes a reply:
```
p 9800 46
p 9801 4e
p 9804 1
p 980e 6
p 9940 54
p 9941 45
p 9942 53
p 9943 54
r 500000
vs
q
```
(`p <addr> <val>` pokes; `9800`/`9801` are `FN_MAGIC0`/`FN_MAGIC1`, `9804` is `FN_ACKSEQ`
pre-satisfying the seq the ROM will send, `980e` is `FN_REPLY_CMD` = ACK, `9940`+ is the
start of the RX payload. `vs` takes a screenshot, written to `shotNNNN.gif`.)

After baking a new demo into the boot ROM, regenerate `firmware/rom.h` from
`intv/fujitest.bin` (byte array named `_bootrom[]`, read big-endian by
`inty_cart.c`'s `Inty_cart_main()`).

-Thom
