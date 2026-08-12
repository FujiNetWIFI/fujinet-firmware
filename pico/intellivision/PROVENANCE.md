# Provenance and licensing status

`firmware/` is a vendored fork of **Minty** by Gennaro Tortone
(https://github.com/gtortone/Minty), forked at commit `29ba4608abcd4cdb2cb292f5c673ce0f2a078481`.
It is an RP2040/RP2350-based Intellivision cartridge that emulates ROM/RAM on the CP-1610
bus, with FAT/LittleFS storage, ROM fingerprinting, and JLP accelerator/flash emulation.
Minty is itself a refactor of **PiRTO II** by Andrea Ottaviani
(https://github.com/aotta/PiRTOII), and its storage layer traces further back to
**A8PicoCart** by Robin Edwards (https://github.com/robinhedwards/A8PicoCart).

## License status: resolved for firmware — Minty is GPLv3

Minty carries its own `LICENSE` file (GPLv3, vendored verbatim at `firmware/LICENSE`), so
unlike the previous PiRTO II-based bring-up on this branch, **the firmware licensing
blocker is resolved**. This vendored copy may be redistributed and merged upstream under
the same terms as the rest of fujinet-firmware.

This does **not** retroactively license the original PiRTO II or A8PicoCart source —
Minty's own author (Gennaro Tortone) is the one who applied a license to his refactor of
that code; we are relying on Minty's license grant, not on any grant from Andrea Ottaviani
or Robin Edwards.

## What's still unresolved: the cartridge PCB

`~/Workspace/PiRTOII-Fuji/HARDWARE.md` and `HARDWARE-PROVENANCE.md` describe a cartridge PCB
derived from PiRTO II's own hardware design (schematic sheet 1 — the Inty bus interface and
reset circuit — copied near-verbatim; board outline, 44-pin edge connector, and enclosure
also derived). **Neither upstream repository has a hardware license either**, and the PCB
question is independent of the firmware question resolved above: Minty being GPLv3 says
nothing about the license status of Andrea Ottaviani's KiCad sources. Do not publish
gerbers, BOM, or STL files, and do not treat the PCB as clear to redistribute, until a
separate grant is obtained from Andrea Ottaviani for the hardware design specifically. (That
PCB was never taken past schematic planning in the KiCad GUI, per HARDWARE.md, so this is
presently a documentation-only concern — `boards/fujicard.h` in this firmware does not
assume any specific committed pin map from that design; see its own comments.)

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
