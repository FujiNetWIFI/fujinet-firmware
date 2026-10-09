# FujiNet for the Nintendo Entertainment System

An RP2354B cartridge that gives the NES a FujiNet: a memory-mapped mailbox
the console talks to, a USB link to an ESP32-S3 running the `fujiversal-nes`
build of fujinet-firmware, and two 512K SRAMs standing in for PRG-ROM and
CHR-ROM/CHR-RAM so that any image the ESP32 fetches -- a FujiNet application
or a commercial game -- runs on the console.

This is the seventh cartridge-mailbox bring-up in `pico/` (Odyssey², Astrocade,
Arcadia, ColecoVision, Channel F, Atari 2600 before it) and shares their
protocol sources byte for byte. What is new is the hardware shape, and this
file is mostly about why it is shaped that way.

## Status

| | Milestone | Artifact | State |
|---|---|---|---|
| M5 | Mapper engine, PIO bank tables, loader ROM | `nesmap.c`, `nes_pio.c`, `nes_tables.pio`, `loader.s` | host tests green (5/5) |
| M0 | Toolchain, header, font | `hello.nes` | runs in MAME, direct and through the loader ROM |
| M1 | One mailbox round trip + reset survival | `fujitest.nes` | adapter config on screen; `resettest` ACKSEQ 1 -> 2 across a soft reset |
| M2 | Network boot through the loader | `fujiboot.nes` | MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE -> DBC push -> PRG SRAM byte-identical to the file; `asteroids.nes` (CHR-RAM) and `wow.nes` (CHR-ROM) play |
| M3 | Directory browser | fujinet-config `nes/` | `cfgtest` browses SD, pages, boots `hello.bin` and a CHR-ROM image through the loader |
| M4 | RP2354B firmware + ESP32 env + CI | `fujines.uf2`, `fujiversal-nes` | both build; core1 in SRAM (`checksram`); the ESP32-S3 image embeds the cart firmware |
| M6 | Soak | synthetic corpus | 19/19: every Tier-1 mapper, PRG and CHR banking checked against the corpus |
| M7 | Real CONFIG + a FujiNet app | fujinet-config `nes/`, 5 Card Stud `src/nes` | CONFIG boots games; 5 Card Stud fetches the live table list and joins a table |
| M8 | Hardware | fujinet-hardware `NES/FujiNet-NES-Rev0` | schematic generated, ERC 0 violations, `check_nets` 418/418 against `nes_cart.h`; no board |

No hardware exists. Everything above was verified in host tests or in a patched
MAME against a live fujinet-pc, never on a console. The MAME device models the
bus as clean bytes, so the M2 sampling model and the PIO latencies are still
host-test and datasheet claims.

## The design in one paragraph

The NES has two buses and the community consensus is that one microcontroller
cannot serve both, so a flash cart needs a CPLD or FPGA. That is true for a
cart that emulates ROM in software. This one does not: PRG and CHR are real
SRAMs the console addresses itself, and the RP2354B's only jobs on the buses
are (1) to serve its own 4K at `$5000` (the mailbox and a 2K loader ROM), 8K of
WRAM at `$6000`, and the reset vectors at power-on -- the family's ordinary
350 ns core1 loop -- and (2) to watch every CPU write so mapper register writes
change the SRAMs' high address lines. Those address lines come from PIO state
machines that are nothing but lookup tables: eight 1K CHR slots indexed by
PPU A10-A12, four 8K PRG slots indexed by CPU A13-A14, each slot a
`set pins` instruction that the mapper engine patches in one atomic store.
Latency from a PPU address change to the bank pins is about 45 ns; a PPU
access is 372 ns. The original EverDrive N8 is exactly this memory
architecture (two CY7C1049 SRAMs) behind an FPGA; here the FPGA's work is a
PIO table, a 74HCT595 for the slow bits, and a 74HCT253 for CIRAM A10.

## Verified facts (do not re-derive)

- NTSC 2A03: 559 ns/cycle; M2 high 350 ns, low 209 ns. The address is
  guaranteed stable only while M2 is high; write data is valid at M2's
  falling edge (nesdev *Cartridge connector*). A15 is not on the edge:
  `/ROMSEL = NAND(M2, A15)`.
- PPU: 186 ns/dot, every memory access two dots; `/RD` low for the second.
- MMC3 scanline counter: an A12 rise after A12 has been low for three M2
  falling edges (nesdev *MMC3*).
- RP2350 PIO: `mov pc, isr` is legal; `set pins` writes up to 5 bits;
  optional side-set gives 4 more (CHR) or 1 more (PRG) in the same word;
  instruction memory is a system-writable "1-write, 4-read register file";
  inputs have a 2-cycle synchroniser; on RP2350B each PIO block sees GPIO
  0-31 or 16-47 (`GPIOBASE`).
- RP2350 GPIOs are "5 V-tolerant (powered) and 3.3 V-failsafe (unpowered)",
  so the cart sits on the 5 V bus with no level shifters, as the INTV and
  Astrocade Rev0 boards do. Pad pulls stay off on every bus pin (RP2350-E9).
- Original EverDrive N8: 512K PRG, 512K CHR, 128K PRG-RAM, mappers 0-255
  minus a handful. Kirby's Adventure (512K+256K, MMC3) is the largest
  licensed NES cart; Metal Slader Glory (512K+512K, MMC5) the largest Famicom.

## Pin map (RP2354B, 48 of 48)

| GPIO | Signal | Notes |
|---|---|---|
| 0-12 | CPU A0-A12 | in |
| 13-20 | CPU D0-D7 | bidirectional, 4 mA |
| 21 | M2 | in; odd pin, so also a PWM slice B input for cycle-counter IRQs later |
| 22, 23 | R/W, /ROMSEL | in |
| 24, 25 | CPU A13, A14 | in; PIO1's PRG index |
| 26, 27, 28 | PPU A10, A11, A12 | in; PIO0's CHR index, A12 also PIO2 (MMC3) |
| 29, 30, 31 | '595 SER, SRCLK, RCLK | out, core0 |
| 32 | /IRQ | driven low or released, never high |
| 33-38 | PRG SRAM A13-A18 | PIO1 out |
| 39-47 | CHR SRAM A10-A18 | PIO0 out |

Everything core1 reads is in GPIO 0-31, one SIO read. PIO0 and PIO1 run with
`GPIOBASE = 16`. USB uses the RP2350's dedicated pins.

'595 bits: `SRAM_EN`, `PRG_WE_EN`, `CHR_WE_EN`, `MIR0`, `MIR1`, `FOURSCREEN`,
`LED`, spare. `MIR1:MIR0` is the 74HCT253 select: 0 = PPU A10 (vertical),
1 = PPU A11 (horizontal), 2 = 0, 3 = 1 (one-screen).

## Decode (all 5 V, HCT)

```
SEL_5000_5FFF   = M2 & ROMSEL & A14 & !A13 & A12      cart-served: mailbox + loader (software)
WRAM $6000-$7FFF                                       cart-served (software)
PRG SRAM /CE    = !( !ROMSEL & SRAM_EN )               $8000-$FFFF
PRG SRAM /OE    = !( R/W & PWR_OK )
PRG SRAM /WE    = !( !ROMSEL & !R/W & PRG_WE_EN )      only while the loader copies
CHR SRAM /CE    = PPU A13                              $0000-$1FFF
CHR SRAM /OE    = !( !/RD & PWR_OK )
CHR SRAM /WE    = /WR | !CHR_WE_EN                     CHR-RAM open, CHR-ROM closed
CIRAM A10       = 74HCT253(A10, A11, 0, 1; sel MIR1:MIR0; /OE = !PWR_OK)
CIRAM /CE       = /A13 | FOURSCREEN
PWR_OK          = console +5V -> divider -> 74HCT14    nothing drives toward a dead console
```

Parts: RP2354B, 2 × AS6C4008-55 (512K×8, 2.7-5.5 V, 55 ns: the PPU path is
~45 ns PIO + 55 ns SRAM against 372 ns), 74HCT595, 74HCT253, 74HCT14, and
two or three 74HCT00/08/32 for the lines above.

## Memory map, as the console sees it

```
$4020-$4FFF  open bus
$5000-$53FF  1K reply window (the loader's slices too)
$5400-$54FF  status page (fuji_mailbox.h)
$5500-$55FF  register writes:  STA $5500+n  => register n = A
$5600-$56FF  raw REGDATA (for the shared decoder; clients do not use it)
$5700-$57FF  TX stream: a write anywhere appends
$5800-$5FFF  loader ROM (2K, cart-served, read-only)
$6000-$7FFF  WRAM, cart-served, mapper-gated
$8000-$FFFF  PRG SRAM through the 4 x 8K table
```

**The one client rule:** only `STA/STX/STY` to `$5500-$57FF`, plain or
indexed. Never an RMW instruction: the 6502 writes the old value back first
and it lands as an event. `tools/checkrom.py` rejects the opcodes; the cart
also recognises the exact signature (same address, adjacent cycles), drops
the dummy write and counts it in `FN_R_DIAG_RMW`.

## Loading

Images are staged on the cart (`fuji_store`: 256K of RAM, or 1.5 MB of flash
above the firmware) exactly as on every sibling, and `fujimail.c` is verbatim.
The SRAMs are volatile and only the console can address them, so the copy is
the 6502's: the client sets `FN_REG_BOOTLOCK` and jumps to the loader ROM at
`$5800`, which asks for the load (`FN_H_REGSEL + FN_HOT_SWAP`) and then, per
1K slice the cart paints into the reply window, copies to
`$8000 + FN_R_LOAD_OFF * 1K` (PRG; the cart moves PRG slot 0 under it) or
through `$2006/$2007` (CHR; the cart moves the CHR window), acking each with
`FN_REG_SLICE_ACK`. About 9.5 cycles a byte: a 32K CONFIG in 0.17 s, a
512K+256K game in ~4 s. Power-on is the same loader entered through the
cart-served reset vector while `SRAM_EN = 0`, copying the CONFIG baked into
the firmware.

Because the loader is cart-served, it survives the copy it performs: no stub
in console RAM, unlike every sibling.

## Mappers

`nesmap.c` is a table of per-mapper handlers that all produce one
`nesmap_out_t` (four PRG banks, eight CHR banks, mirroring, WRAM and CHR
write gates). Three consumers read it: the firmware (PIO tables + '595), the
MAME device (its slot arrays), and `test_nesmap.c`, which fuzzes 4000 random
writes per configuration against transcriptions of MAME's own handlers.

Tier 1, implemented: 0 NROM, 1 MMC1 (with the consecutive-write rule and
SUROM's 512K), 2 UxROM, 3 CNROM, 4 MMC3, 7 AxROM, 11 Color Dreams,
30 UNROM-512, 34 BNROM/NINA-001, 66 GxROM, 71 Camerica/BF9097, 206 Namcot 108.

Tier 2, table entries to come: VRC1-7 (no audio), FME-7 (no audio), N163 (no
audio, no nametable-as-CHR), Bandai, Jaleco, Irem, Taito, RAMBO-1, Sunsoft-3/4,
the discrete-logic boards. Hardware limits, plainly: MMC5's extended
attributes / ExRAM nametables / vertical split and the MMC2/MMC4 tile latches
need the PPU address bus at PPU speed, which this pin map does not carry;
expansion audio has no path on a 72-pin NES; 10NES is an external CIClone.

## Building

```sh
./build.sh                       # loader.bin + hello/fujitest/fujiboot .nes (cc65 2.19)
./build-cart.sh                  # firmware/build-fujines_rp2354b/fujines.uf2
make -C firmware/host_test       # the five host tests, no SDK
./emu/apply.sh ~/Workspace/mame  # graft the device; then make REGENIE=1
./run.sh fujitest                # MAME, against fujinet-pc's BoIP on 127.0.0.1:9995
./run.sh fujitest resettest      # headless, driven by emu/resettest.lua
./run.sh config cfgtest          # drive CONFIG with the joypad: open SD, boot hello.bin
CFG_FILE=wow.bin CFG_EXPECT=image:$SD/wow.bin ./run.sh config cfgtest   # page, boot, byte-compare both SRAMs
```

`FUJINET_LOADER=1 ./run.sh hello` sends the `-cart` image through the loader
ROM instead of running it directly, as the cart does at power-on.
`FUJINET_DEBUG=1` logs every transaction; `FUJINET_BOOTDUMP=path` writes the
SRAM contents after a load for a byte compare.

## What a scope has to settle, one session

In-band diagnostics first (`FN_R_ERR`, `FN_R_DIAG_RMW` on screen), scope last.

1. M2 rise to cart data valid on a mailbox read, and how long write data
   holds after M2 falls (the last-sample-while-high model is PROVISIONAL).
2. PPU A10-A12 change to CHR bank-pin settle (expect ≤ 45 ns) and SRAM data.
3. An MMC3 game with 1K banking and a status-bar split, to settle
   `MMC3_A12_FILTER_NS` and /IRQ latency.
4. UNROM `JMP` into a just-selected bank.
5. `PWR_OK` with the console off and USB on.
6. The power-on race with no USB attached: the RP boots in a few ms and the
   2A03 fetches its reset vector early; a CPU that wins reads open bus until
   Reset. With the ESP32 on USB the RP is already up.

## Known limits of this bring-up

- A console Reset during a load restarts the 6502 through half-written
  vectors; power-cycle to recover (the M2 watchdog then reloads CONFIG).
- Four-screen mirroring is a '595 bit, not yet wired to a CHR A18 mux.
- Battery saves live in the cart's WRAM and are not persisted yet.

## Licence notes

`emu/fujinet.cpp/.h` are BSD-3-Clause for MAME. The firmware and clients are
under the repository licence. The design references the EverDrive N8 only for
its memory architecture (nesdev wiki); nothing is copied from it.
