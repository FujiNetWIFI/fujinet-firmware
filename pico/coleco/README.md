# FujiNet for the ColecoVision

An RP2040 cartridge that serves the ColecoVision's 32K cart window and, through
it, a FujiNet: browse a TNFS host from the console, boot images over the
network, and let a FujiNet-aware image keep talking to the network afterwards.
The protocol is verified end to end in emulation — a MAME cartridge device runs
the cartridge's own protocol sources against a real `fujinet-pc` — and
cartridge hardware does not exist yet.

Fifth in the cart-slot family, after
[Intellivision](../intellivision), [Odyssey 2](../o2),
[Astrocade](../astrocade) and [Arcadia 2001](../arcadia). Structurally it is
closest to the Astrocade: one pre-decoded enable per bus cycle, both mailbox
directions riding the read path.

## The port, and why everything follows from it

The 30-pin cartridge connector carries **A0–A14, D0–D7, four active-low chip
selects (`/8000 /A000 /C000 /E000`, one per 8K block), GND and +5V**. Nothing
else: no `/RD`, no `/WR`, no `/MREQ`, no clock, no reset. Those selects come out
of a 74LS138 (U5) with A13/A14/A15 on the select inputs and **`/MREQ` and
`/RFSH` as its enables** — verified against the reverse-engineered netlist of
`sparkletron/righteous_tentacle_colecovision` and MAME's own
`bus/coleco/cartridge/exp.h`. Two consequences shape the whole design:

- **The cartridge cannot tell a read from a write.** Which is exactly why every
  real ColecoVision mapper — MegaCart, Activision, X-in-1 — takes its bank
  number from the *address*: the hardware left them no choice either. Only the
  Opcode SGC latches a data byte, and it gets special handling.
- **Refresh cycles never assert a select.** The Astrocade port had to defend
  against the Z80's R register spraying phantom reads across its hotspot pages;
  here the console filters them out for us.

So both directions of the mailbox are cartridge reads:

- **console → cart**: reads inside a hotspot window; A0–A7 of the address *is*
  the payload byte. Reading `$FD05` arms register 5, reading `$FE2A` sets it to
  `0x2A`, reading `$FF41` appends `'A'` to the outgoing stream.
- **cart → console**: bytes core0 paints into the window it already serves. By
  the time the console reads them they are just ROM.

The window map, register file, interlocks and boot protocol live in
**`firmware/include/fuji_mailbox.h`** — the single source of truth for the
RP2040 firmware, the MAME device, and (hand-mirrored in `testrom/fujilib.h`)
the Z80 client.

| Image offset | Console | What |
|---|---|---|
| `0x0000-0x77FF` | `$8000-$F7FF` | client ROM (30K budget) |
| `0x7800-0x7BFF` | `$F800` | the whole 1K reply, in one piece |
| `0x7C00-0x7C0C` | `$FC00` | status: ACKSEQ, ERR, RXLEN, BOOT_*, magic `'F' 'N'` |
| `0x7CFC` | `$FCFC` | `"FUJI"` claim signature |
| `0x7D00-0x7DFF` | `$FD00` | hotspot: REGSEL (`0xFE` = armed-only ROM swap) |
| `0x7E00-0x7EFF` | `$FE00` | hotspot: REGDATA (inert without a preceding REGSEL) |
| `0x7F00-0x7FFF` | `$FF00` | hotspot: TX stream (and, mailbox-dead only, every game mapper's bank selects) |

**The reply is one 1K block, not a paged 256-byte slice.** Every other port in
the family pages because its window was too small; here the window is 32K and
the *console* is what is scarce — about 700 usable bytes of RAM once OS7's
tables and the stack are paid for. Handing the client the whole reply as
directly addressable cartridge ROM removes a bounce buffer and a polling
handshake from every transaction, and is the difference between a directory
browser fitting and not.

## Milestones

| | | |
|---|---|---|
| **M0** | toolchain, header, display | `hello` — 32×24 text through OS7 (`mode_1`, `load_ascii`, `put_vram`) |
| **M1** | one real round trip | `fujitest` — live SSID, IP and firmware version from `GET_ADAPTERCONFIG_EXTENDED`; reset survival proven (a soft reset produces `seq=2`, not a replay of `seq=1`) |
| **M2** | network boot | `fujiboot` — MOUNT_HOST → SET_DEVICE_FULLPATH → MOUNT_IMAGE → DBC push → swap → *Carnival* boots and runs its own OS7 options screen |
| **M3** | CONFIG | `fujicfg` — pick a host, walk its root, boot what the cursor is on; input entirely through OS7's POLLER, driven headlessly by `emu/drive.lua` |
| **M5** | mappers | all five implemented in `colmap.c` and checked byte-for-byte over the whole 32K window against verbatim transcriptions of MAME's own handlers; the live A/B run against MAME's stock devices is not yet wired up |
| **M7** | soak | **192/192** — every cartridge in the No-Intro set pushed, swapped, and byte-compared, window and stream both |

## Running it

```sh
# 1. Protocol tests: no hardware, no SDK, no emulator
make -C firmware/host_test

# 2. Console-side clients -> build/*.bin (exactly 32768 bytes, claim stamped)
./build.sh                        # hello fujitest fujiboot fujicfg
BOOT_PATH=/soak.rom ./build.sh fujiboot

# 3. The MAME model. REGENIE=1 is needed the FIRST time (bus.lua changed);
#    re-run apply.sh after EVERY edit here -- MAME builds the copies.
./emu/apply.sh ~/Workspace/mame
make -C ~/Workspace/mame -j$(nproc) NOWERROR=1 REGENIE=1

# 4. Against a live fujinet-pc (its BoIP listener on 127.0.0.1:9995)
./run.sh fujitest                                    # windowed
./run.sh fujitest --headless "SSID"                  # one-line verdict
SCREEN_AT=16 FUJINET_DEBUG=1 ./run.sh fujiboot --headless
DRIVE_DOWN=9 ./run.sh fujicfg --drive                # browse and boot, headless

# fujiboot's verdict string depends on WHICH image is at $BOOT_PATH on the
# host -- "SKILL 1" is Carnival's OS7 options screen. tools/soak.sh reuses
# /soak.rom for every image it tries, so put back whatever you were testing
# with before expecting that string again.

# 5. Firmware
./build-cart.sh fujicoleco        # -> firmware/build-fujicoleco/fujicoleco.uf2
```

## Notes and gotchas

Things that cost real time here, kept so they cost it only once.

- **`(void)volatile_read;` does not compile to a read.** sccz80 emitted the
  address calculation for `(void)FN_REGSEL[reg];` and then threw the load away
  — `ld de,64768` immediately overwritten by `ld de,65024` — so the REGSEL half
  of every register write silently did not happen and no transaction ever
  launched. Every mailbox touch now stores through `FN_TOUCH`, into a volatile
  sink. If `fujilib.c` is ever rewritten, check the generated code:
  `zcc +coleco -O2 -a fujilib.c` and count the loads.
- **Index the reply window through a pointer variable, never `FN_REPLY[i]`.**
  Same compiler, second helping: sccz80 indexes the macro's cast constant
  differently from a `volatile unsigned char *` local and gets it wrong. The
  identical expression drew filenames correctly through a local and read back
  zero through the macro — which showed up as a directory listing that looked
  perfect but whose end-of-directory test never fired.
- **Do not read past the end of a directory.** `fujiDevice` returns two `0x7F`
  bytes when `dir_nextfile()` runs out, but a fujinet-pc SD host hands back
  `..` over and over instead — and once you have done that, every subsequent
  `SET_DIRECTORY_POSITION` NAKs for the rest of the session. `fujicfg` stops on
  `0x7F 0x7F`, `.` and `..` alike.
- **Two FujiNet cartridges cannot share a MAME tree without renaming.**
  `fujimail.c`, `fujibus.c` and `fujitcp.c` are byte-identical across the
  Astrocade, Arcadia and ColecoVision ports — but each copy is compiled against
  its *own* `fuji_mailbox.h`, where `FN_R_MAGIC0` is `0x1C09` on one platform
  and `0x7C09` on another. With more than one grafted, the linker picks a single
  definition of `fujimail_paint()` for the whole binary and the others paint the
  wrong offsets: the socket connects, the cartridge answers, and the client
  reads `0xFF` forever. `emu/apply.sh` generates `fujins.h` to prefix every
  shared symbol. **The astrocade and arcadia grafts do not do this yet and are
  exposed to the same failure whenever both are applied to one tree.**
- **The vblank interrupt is on `/NMI` and `DI` will not stop it.** It *will*
  land in the middle of a REGSEL/REGDATA pair. That is harmless — REGSEL stays
  armed, the TX stream is append-only — provided the handler never reads
  `$F800` and up. `tools/checkrom.py` enforces the image half of that rule.
- **The RAM mirror aliases onto the mailbox.** The console's 1K of RAM repeats
  through `$6000-$7FFF`, so a RAM access at `$7C00-$7FFF` puts exactly the same
  bits on A0–A14 as a cartridge read of `$FC00-$FFFF`. A15 is not on the
  connector. This is why core1 serves data speculatively off the bare address
  but commits side effects only when the chip select confirms the cycle.
- **The swap stub must run from RAM, and must silence the VDP first.** The BIOS
  vectors NMI straight to `$8021` — a byte that belongs to the *next* image the
  moment the swap happens. The stub clears VDP register 1, reads the status port
  to drop an already-latched interrupt, pads, swaps, resets all eight VDP
  registers (the `$55AA` path is `LD HL,($800A) / JP (HL)` with no
  initialisation of anything), silences the SN76489, and jumps to `$0000`.
- **z88dk's `$55AA` comment is backwards.** `lib/target/coleco/classic/rom.asm`
  emits `55 AA` and calls it "Title screen + 12 second delay". The BIOS at
  `$0072` compares and jumps immediately: `55 AA` is the *skip* case. The bytes
  are right for a CONFIG client; the comment is not.
- **MAME's Lua notifier is an RAII token.** `emu.add_machine_frame_notifier`
  returns a subscription that unsubscribes at the next garbage collection if you
  drop it — so a callback scheduled two seconds out fires and one scheduled ten
  seconds out silently never does. `emu/screen.lua` keeps it in `_G`.
- **The sound chip powers up buzzing.** All four SN76489 channels come up at
  attenuation 0 with their frequency registers at zero, and the BIOS title
  screen is what normally silences them — which a `$55AA` client skips. Every
  client calls `snd_init()` first; `fujisnd.h` says why that is mandatory rather
  than tidy.
- **`machine.time.seconds` is the whole-seconds FIELD, not the time.** Use
  `machine.time:as_double()`. Comparing against `.seconds` quantises every
  interval to a full second, which held `drive.lua`'s 0.30 s presses for a whole
  one and let auto-repeat walk the cursor several rows per press — so
  `DRIVE_DOWN=9` was landing on the right file only because the cursor was
  pinned at the bottom of the list.
- **Pace auto-repeat by vblanks, not by `in_read()` calls.** The main loop polls
  input thousands of times a second, so a per-call counter runs the repeat at
  loop speed and throws the cursor across a page in one press. The NMI bumps a
  frame counter and `in_read()` ages the delay only when it changes. Both of
  these only became visible once cursor movement had a click — the bug was
  audible before it was ever noticed on screen.
- **MAME re-executes the autoboot script on every soft reset** — so harness
  state that must survive a reset lives in `_G` too — but the emulated clock
  keeps running across one. A sample scheduled at `SCREEN_AT` after a reset at
  the same time therefore fires one frame later, catching the client before it
  has re-run and reading VRAM that still shows the previous run's screen: a
  convincing pass on content, and a spurious failure on everything else.
- **MAME must be run from its own tree.** It resolves `rompath`, `pluginspath`
  and the Lua search path against its working directory; run it from anywhere
  else and `-autoboot_script` is ignored with no error and no output.
- **`.col` needed adding to the ESP32's `discover_mediatype()`.** The No-Intro
  ColecoVision set uses that extension throughout, so without it not one
  cartridge in the corpus would ever have reached `MediaTypeROM`.
- **The `.cfg` sibling matters here, unlike everywhere else in the family.**
  Three Activision cartridges are 64K and two Opcode SGC cartridges are 128K,
  and both sizes are also MegaCart sizes — nothing in the image distinguishes
  them. A one-line `mapper=` settles it, and the hint is consumed at the ROM
  stream's close so a later mount with no sibling cannot inherit it.
- **No flash tier.** The Astrocade serves a 1.789 MHz Z80 and can afford an XIP
  cache miss; this port has ~373 ns from address-valid to data-required and
  cannot. A banked image must be in SRAM, which caps the cartridge at 128K:
  24 of 31 MegaCart titles, both SGC titles and all three Activision ones fit;
  the 256K/512K MegaCarts and both X-in-1 images do not. Lifting that is an
  RP2350 decision, not a firmware one.

## The eventual hardware

A stock Pico covers the port exactly, with nothing to spare: A0–A14 on GP0–14,
D0–D7 on GP15–22, the combined chip select on GP26, the data-buffer direction on
GP27, console power sense on GP28. Before committing a PCB:

- The data buffer must be a **dual-supply translator (74LVC8T245, VCCA 3.3V /
  VCCB 5V)** or a **74LVC245A run at 3.3V** — its I/Os are 5.5V tolerant and its
  VOH clears every 74LS input on the bus. *Not* a 74HCT245 on the 5V rail: the
  SGC path turns the buffer around, and a 5V part would then drive 5V into the
  RP2040. Combine the four chip selects with a 3.3V **74LVC08** pair rather than
  a 5V 74HCT21 — same tolerance argument, a third of the delay.
- Wire the buffer's **`/OE` to the combined chip select in hardware**, not to a
  GPIO. That is what keeps software out of the select-to-data path and leaves
  ~373 ns of budget instead of ~200.
- **Power sense earns its pin twice**: it is the only way to tell a console
  power-cycle from a RESET press (the reset line does not reach the cartridge),
  which is how the firmware gets back to CONFIG after booting a game; and it
  holds the buffer facing away from an unpowered console, whose `/138` outputs
  all read as "asserted" when its rail is down.
- The SGC data-sampling window is the one thing MAME structurally cannot
  validate — `coleco_state::cart_r`/`cart_w` have perfect read/write
  discrimination that the connector does not. Treat it as unproven until a
  scope says otherwise.
