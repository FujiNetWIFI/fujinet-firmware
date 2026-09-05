# FujiNet for the Emerson Arcadia 2001

A FujiNet bring-up for the Emerson Arcadia 2001 (Signetics 2650 CPU +
Signetics 2637 UVI), in the shape of the [Bally Astrocade one](../astrocade):
an RP2040 cartridge serves the ROM window and a mailbox made entirely of
reads, bridged over USB to an ESP32-S3 running fujinet-firmware's
`fujiversal-arcadia` build. Everything here is verified in a patched MAME
against a live `fujinet-pc`; no console or cartridge hardware exists yet.

## The port, and why everything follows from it

The Emerson-family cartridge connector carries **A0–A13, D0–D7, +5V and
GND — nothing else**. There is no write strobe, no clock, no OPREQ, and no
reset line. **A12 is the active-low chip select** (a mask ROM's `/CE`) and
A13 picks which 4K block is addressed. A14 is not on the connector, so
`$4000` aliases block 1 and `$6000` aliases block 2 — hotspots included.

So, exactly as on the Astrocade, **both directions ride the read path**:

- **console → cart** is a read inside a hotspot window, where the low
  address byte *is* the payload byte;
- **cart → console** is bytes the RP2040 painted into the window it already
  serves — by the time the console reads them they are just ROM.

The 2650 sees the cartridge as two 4K blocks: image `0x0000–0x0FFF` at CPU
`$0000` and image `0x1000–0x1FFF` at CPU `$2000`. The mailbox lives in the
top of the image (`0x1B00–0x1FFF`, i.e. CPU `$2B00–$2FFF`), the same
offsets the Astrocade uses — which is why `fujimail.c`, `fujibus.c` and the
whole wire protocol are shared verbatim and only `fuji_mailbox.h` forks.

`firmware/include/fuji_mailbox.h` is the single source of truth (the RP2040
firmware, the MAME device, and the hand-mirrored `testrom/fujilib.inc` all
follow it). Three gates keep stray reads harmless: a REGDATA read disarms
after one use, a transaction launches only on a fresh nonzero sequence
number, and booting an image that does not claim the mailbox kills hotspot
decode for the session.

## Milestones (all verified in MAME vs a live fujinet-pc)

- **M0 — toolchain + display.** `hello.asm` proves the 2650 assembler
  (Macroassembler AS), the cart header, the 2637 charset, user-defined
  characters and the keypad.
- **M1 — `fujitest`.** `GET_ADAPTERCONFIG_EXTENDED`; the live SSID, firmware
  version and IP address on screen. A console reset re-runs it with a fresh,
  incrementing sequence number (never a replayed reply). Doubles as the
  future EPROM diagnostic: burned to an 8K EPROM it shows `NO FUJINET CART`.
- **M2 — `fujiboot`.** `MOUNT_HOST` → `SET_DEVICE_FULLPATH` → `MOUNT_IMAGE`;
  the ESP32 streams the image back over the DBC device while the mount reply
  is still outstanding; the client arms the swap, runs an 11-byte stub from
  RAM that reads the swap hotspot, and the game boots. Jungler plays.
- **M3 — `fujicfg` (CONFIG).** Pick a host (`READ_HOST_SLOTS`), browse it
  (`OPEN_DIRECTORY` / `READ_DIR_ENTRY` with a cursor and paging), boot what
  you land on. The real full-feature CONFIG (wifi / hosts / rename / browse
  / info / boot) lives in `fujinet-config/arcadia/`.
- **M5 — soak.** All 48 carts in the ROM library stream through byte-for-byte
  and boot: `tools/soak.sh` → **48/48**.

## Layout

```
firmware/            RP2040 cartridge firmware (pico-sdk)
  include/fuji_mailbox.h   THE SPEC
  src/arcadia_cart.c       core1: serve the bus (the one hardware-specific bit)
  src/fuji_cart.c          serve state + the boot swap (one pointer store)
  src/arcmap.c             image gate/plan/apply + the connector decode
  src/{fujimail,fujibus,fujibus_usb}.c   shared verbatim across platforms
  host_test/               desktop tests (no hardware, no SDK)
emu/                 MAME cart device + graft script + lua harnesses
testrom/             fujitest / fujiboot / fujicfg + fujilib.inc + fujidisp.inc
tools/               checkrom, mkimage, mkromh, checkdepth, soak.sh
build.sh             assemble a client (Macroassembler AS)
build-cart.sh        build the RP2040 firmware, baking the client in
run.sh               run a client in MAME against a live fujinet-pc
```

## Running it

Prerequisites: a MAME tree with the device grafted in (`./emu/apply.sh
/path/to/mame`, then `make -C /path/to/mame -j$(nproc) REGENIE=1`), a
`fujinet-pc` reachable on `$FUJINET_TCP` (default `127.0.0.1:9995`), and
Macroassembler AS — a native `asl`/`p2bin` on `PATH` (or in `~/asl`), else
the Windows binaries under wine (`build.sh` finds them). The `arcadia`
driver needs no BIOS ROMs.

```sh
./build.sh fujitest fujiboot fujicfg     # assemble the clients
./run.sh fujitest                        # milestone 1
BOOT_HOST=0 BOOT_PATH=/jungler.bin ./build.sh fujiboot
./run.sh fujiboot                        # milestone 2
./run.sh fujicfg                         # milestone 3 (the CONFIG)
tools/soak.sh                            # the whole ROM library
```

The firmware: `./build-cart.sh` regenerates `fujiconfigrom.h` from the
baked-in client (the real `fujinet-config/arcadia` CONFIG if its
`build/config.bin` is copied here, else the `fujicfg` stand-in) and builds
`firmware/build-fujiarcadia/fujiarcadia.uf2`.

## Notes and gotchas (2650, AS, the 2637)

- **The return stack is 8 deep and on chip** — there is no RAM stack, and a
  call graph deeper than 8 silently corrupts returns. `tools/checkdepth.py`
  enforces the budget (the clients top out at 5). The top level is a
  dispatch of absolute branches, not calls; the transport primitives are
  leaves.
- **Non-branch memory instructions reach only their own 8K page.** Page-0
  code cannot `LODA $2xxx`; the mailbox is reached through 15-bit indirect
  pointer words (`LODA,R0 *FSELP,R1`). Branch absolutes (`BCTA`/`BSTA`)
  carry the full 15 bits — and are needed for any jump farther than the ±63
  bytes a relative branch reaches.
- **`EORZ Rn` computes `R0 = R0 ^ Rn`** — it acts on R0, not Rn. To zero a
  register other than R0, use `LODI,Rn 0`. (Padding a path with `EORZ R1`
  instead spent the loop appending the last name character.)
- **`DW` emits little-endian, but the 2650 fetches indirect pointers
  big-endian.** Every pointer word the CPU dereferences uses the `DWBE`
  macro.
- **The TX-append idiom carries the byte in the index register**: reading
  `FN_H_DATA + v` appends `v`, so `LODA,R0 *FTXP,R1` appends R1.
- **The 2637 has a 64-glyph character ROM and no lowercase** (and no
  `/ - _ :`). Screen bytes are 2 colour bits + a 6-bit glyph, and a byte of
  `$C0` flips the row into block-graphics mode — so text is only ever
  painted with the colour bits clear. `fujidisp.inc` translates ASCII and
  supplies the missing punctuation and the cursor chevron as user-defined
  characters (all 8 are free; the clients use no sprites).
- **Frame sync is the SENSE pin** (`TPSU $80`) — the UVI's vertical retrace.
  There are no interrupts.
- **The connector has no read strobe**, so `core1` cannot spin until an
  Enable drops as the Astrocade loop does (A12 stays low across consecutive
  instruction fetches). It serves combinationally and de-duplicates hotspot
  events on a change of the full A0–A13 pin state, with a two-sample
  stability gate. That decision is `arcadia_bus_observe()` in
  `arcadia_cart.h`, fuzzed on the desktop by `host_test/test_busedge.c`.

## Hardware status

None built. The 5V TTL bus is not RP2040-tolerant, so a real cartridge needs
level shifters on the 14 address inputs and 8 data outputs; the pin map in
`firmware/include/arcadia_cart.h` reserves GP22/GP26/GP27 for a self-test
trigger, a console-5V power sense, and a debug UART. `fujitest.bin` is a
valid 8K EPROM image and is the intended first hardware check.
