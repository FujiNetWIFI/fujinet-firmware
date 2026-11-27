# pico/trs80m1

FujiNet for the TRS-80 Model I: an RP2354B card on the keyboard unit's 40-pin expansion edge,
in place of the Expansion Interface, that serves 32K of RAM, an FD1771 whose drives are FujiNet
slots, the EI's interrupt latch and heartbeat, its printer port, the RS-232 board and a FujiBus
pipe, and relays them over USB CDC to an ESP32-S3 on the same card, the `fujiversal-trs80m1`
board.

## How it fits

The ESP32-S3 side is `BUILD_RS232` with USB host transport, and Model I disks mount as
`JV1Disk` ([lib/media/disk/](../../lib/media/disk/)). `trs80m1Fuji` puts CONFIG in slot 1 from
the S3's flash as `/autorun.jv1`. The card answers:

- 8000h-FFFFh, 32K of RAM in the RP's SRAM;
- 37E0h-37EFh, the EI: the interrupt latch (40 Hz heartbeat, FD1771 INTRQ) and drive select,
  the printer at 37E8h, and the FD1771 at 37ECh, whose drives 0-3 are slots 1-4, so the Level II
  ROM boots slot 1;
- ports E8h-EBh, the RS-232 board, as the FujiNet modem;
- ports D0h-D3h, the FujiBus pipe (`pico/common/include/fuji_pipe.h`) for programs and
  CONFIG, whose CONTROL also takes the card's settings.

RAM, the EI and the RS-232 board each turn on or off from CONFIG (kept in the RP's flash), and a
DIP switch forces one off, for a machine that has a real EI. A Model I switched back on asks
the ESP32 for CONFIG in slot 1 again; RESET reboots whatever slot 1 holds.

## Build

```sh
./build.sh                          # Z80 clients, test disks, /CMDs -> build/client
                                    # (needs ~/Workspace/z88dk; FUJI_CONFIG=config.cmd adds autorun.jv1)
./build-cart.sh [--baked]           # RP2354B firmware -> build/fw-*/fujitrs80m1.uf2
make -C firmware/host_test          # host tests
tools/fetch-disks.sh                # LDOS 5.3.1, NEWDOS/80 2.0, TRSDOS 2.3 JV1 images -> build/disks
tools/libtest.sh                    # fujinet-lib's tests as boot disks -> build/libtest
emu/apply.sh ~/Workspace/mame-trs80 # graft the card into MAME as trs80fn, then:
                                    #   make SUBTARGET=trs80 SOURCES=src/mame/trs/trs80.cpp
./run.sh ldos [--async] [--native] [--trace] [--set KEY=VALUE]
./run.sh ldos --show                # the same in a MAME window at real speed, left open
./run.sh boot client:fnhello.dsk disk:ldos531.dsk   # a window with these in slots 1, 2...
```

`run.sh` talks to a fujinet-pc on `$FUJINET_TCP` (default `127.0.0.1:1980`), whose BoIP port
takes one client at a time, and drives `emu/scenarios/<name>.steps` with `emu/m1text.lua`.
`client:` names come from `build/client`, `disk:` from `build/disks`; `client:autorun.jv1` is
CONFIG. The `fujiversal-trs80m1` zip carries the card firmware, which the S3 flashes over
PICOBOOT.

## Notes

- core1 polls the strobes and calls nothing; the '245's output enable is gated by RD*/IN* in
  hardware, so the bus is let go at the strobe's edge whatever core1 is doing.
- Read timing is checked only by `test_timing` against datasheet numbers: the stock 1.77 MHz Z80
  is met in the worst case, 2.66 and 3.55 MHz speed-ups only when no FD1771 post lands as a
  strobe falls.
- The FD1771 is the instant-data model: sector bytes are there when the CPU polls for them; steps,
  index pulses and the motor one-shot run in real time. JV1 keeps no address marks, so cylinder
  17, the directory, reads as FA.
- The pin map is provisional until the board exists.
