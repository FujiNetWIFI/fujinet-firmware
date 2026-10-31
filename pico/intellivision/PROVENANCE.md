# Provenance and licensing status

`firmware/` is a vendored fork of **Minty** by Gennaro Tortone
(https://github.com/gtortone/Minty), forked at release **2.4**
(`887840e8ac84a0ebeaef843dbff55768f50be564`).
It is an RP2040/RP2350-based Intellivision cartridge that emulates ROM/RAM on the CP-1610
bus, with FAT/LittleFS storage, ROM fingerprinting, and JLP accelerator/flash emulation.
Minty is itself a refactor of **PiRTO II** by Andrea Ottaviani
(https://github.com/aotta/PiRTOII), and its storage layer traces further back to
**A8PicoCart** by Robin Edwards (https://github.com/robinhedwards/A8PicoCart).

## License status

Minty (`firmware/LICENSE`) and PiRTO II are both GPL-3.0, so this fork is GPL-3.0 like the
rest of fujinet-firmware.

The one gap is A8PicoCart, which is unlicensed. Files whose header says "parts of code are
directly from the A8PicoCart project" carry Robin Edwards's code, which neither Minty's nor
PiRTO II's licence can cover. `pico/o2` has the same gap; see
[`../o2/firmware/PROVENANCE.md`](../o2/firmware/PROVENANCE.md).

## Hardware

PiRTO II's PCB is GPL-3.0 as well. The FujiNet-INTV-Rev0 cartridge (fujinet-hardware
`INTV/FujiNet-INTV-Rev0`) follows PiRTO II's outline and shell-post holes, and PiNTY CARD
(CERN-OHL-W-2.0) for its edge and support circuit.

## What we're adding

FujiNet integration (the mailbox protocol at Intellivision RAM `$9C00-$9F3F`, bridging the
RP2040/RP2350 to an ESP32-S3 running fujinet-firmware's `fujiversal-rs232` build over USB
CDC) is new code, original to this repository, and is GPLv3 like the rest of
fujinet-firmware. See `README.md` in this directory for the design, and
`firmware/fuji_mailbox.h` for why the mailbox lives at `$9C00` rather than `$9800` as it did
on the PiRTO II-based prototype: Minty's JLP emulation claims all of `$8000-$9FFF` as RAM,
so the mailbox was moved to the top of that window and shrunk to leave the rest free for
JLP games.

Minty's own on-cart launcher (SD/flash browsing, menu UI) is **removed** in this vendored
copy — the FujiNet mailbox and the network-side CONFIG program
(`~/Workspace/fujinet-config/intv/`) replace it entirely. See README.md.
