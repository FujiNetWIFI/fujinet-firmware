# FujiNet on the Atari 2600

The sixth cart-mailbox bring-up, after the Odyssey², Astrocade, Arcadia 2001,
ColecoVision and Channel F. Same protocol, same `fujimail.c`, same
verify-in-MAME-before-any-hardware discipline — on a console with a **4K
window, 128 bytes of RAM, and no framebuffer**.

This tree sits alongside gtortone's PlusCart-Pico import (`src/`, `include/`,
`data/`, `images/`, `scripts/`, `patches/`, `attic/`), which is untouched and
remains the electrical and mapper reference. Its GPLv3 licence covers the bus
technique lifted below; see `LICENSE`.

## What is different about this console

**It has real writes, and it should not.** The cartridge connector carries
A0–A12, D0–D7, +5V and GND — no R/W line, no clock, no chip select beyond A12.
But the 6507 drives the data bus during a store, so the cart declares certain
pages write-only, never drives them, and recovers the byte by parking on the
stable address and keeping the second-to-last data sample:

```c
while (ADDR_IN == addr) { data_prev = data; data = DATA_IN; }
append(data_prev);
```

That is not invented here. It is how every shipping PlusROM game talks to its
cart and how every Superchip cart implements its RAM — both idioms are in
`src/cartridge_emulation.cpp` in this same directory.

**But the Channel F's simplification does not follow.** That cart can tell a
store from a fetch (`ROMC 05` vs `ROMC 02`), which is why its header can say
reads of the register pages are inert. This cart sees an address and nothing
else, so **a read of a write port is indistinguishable from a write to it**.
The REGSEL/REGDATA arm-then-commit pair is therefore kept, and here it is the
defence rather than protocol shape.

**The one structural gift: the 6507 has no interrupts.** MAME's own
`m6507.cpp` says it — *"28-pin package, address bus is 13 bits, no NMI, no SO,
no SYNC"* — and nothing on the 2600 is wired to /IRQ. Every stray access comes
from the client's own instruction stream, which is what lets
`tools/checkrom.py` be a static proof where on the ColecoVision it could only
be a heuristic.

## The window

Sixteen pages, total, for everything.

| Console | Role |
|---|---|
| `$1000-$17FF` | 2K banked client code |
| `$1800-$1AFF` | six 128-byte text planes, composed by the cartridge |
| `$1B00-$1CFF` | 512-byte reply window (2 slices) |
| `$1D00-$1DFF` | control page — **write-only, never driven** |
| `$1E00-$1EFF` | TX stream — **write-only, never driven** |
| `$1F00-$1FFF` | status, claim, fixed tail, and the vectors |

Three decisions carry it, and each buys a page back:

- **`FN_H_REGDATA` and `FN_H_DATA` are not console addresses.**
  `fujimail_read_hotspot()` switches on `offset >> 8` and only needs three
  *distinct page numbers*; the bus layer synthesises the events. Two of the
  three live outside the window entirely.
- **The reply is 512, not 256 or 1024.** `fujinet-battleship`'s `GMAXLEN` is
  509, so 512 is the size at which the flagship client never pages a slice at
  all.
- **The claim and the vectors live in the fixed half**, so a console RESET is
  survivable whatever bank is mapped low.

## The display

The console has no framebuffer, so the **cartridge composes the glyphs** and
publishes them as six 128-byte planes; the 6502 kernel does nothing but
indexed loads into `GRP0`/`GRP1`. 21 rows × 12 columns.

The 128-byte alignment is load-bearing: it is what makes `lda plane,y` always
four cycles and never five, so the kernel needs no zero-page pointers and no
per-row setup — Y simply counts 0…125 down the whole screen.

**The kernel's constants are the coordinates of a one-pixel-wide window.**
Every wrong setting still looks like text, which is why `emu/dispcheck.py`
decodes the raster back into plane bytes and byte-compares it against the
renderer instead of anyone squinting at a screenshot. Three real bugs came out
of that: a missing seventh `GRP` write (with VDELP, six data bytes need seven
writes), an end-of-frame drain that toggled VDELP off and on and so *restored*
the stale registers, and a `ldx`-vs-`tsx` cycle — the copies are 2.67 cycles
apart and a zero-page store is 3, so the four late writes drift a full cycle
and that one cycle decides whether the schedule closes at all. The `tsx` trick
and the `sty GRP0` seventh write are batari Basic's, transcribed rather than
re-derived.

**Three glyph pairs were bit-identical at 3x5** and had to be redrawn: `0`/`O`,
`5`/`S`, `C`/`[`. That is not a curiosity in a filename browser -- `SOAK.BIN`
and `50AK.BIN` were the same picture, to the decoder and to a person. `0` is
now rounded against `O`'s square, `S` curved against `5`'s flat top, `[`
half-width against `C`; the set is audited exhaustively and the only remaining
collision is `?` against DEL, which is deliberate since unknown characters
render as `?`.

Harnesses that look for text on screen therefore compare **rendered forms**,
not decoded text: they ask what a name *would* look like rather than what the
screen decodes to, because decoding is lossy at this size and always will be.

## Milestones

| | | |
|---|---|---|
| **M0** | the cart-rendered display | **done** — `emu/dispcheck.py`: 756/756 bytes |
| **M-novel** | the bus decode and write-sampling model | **done** — `host_test/test_busio.c` |
| **M1** | the mailbox round trip and reset survival | **done** — live SSID/IP/version on screen; ACKSEQ 01 → 02 across a reset |
| **M2** | network boot, byte-identical | **done** — 4096/4096 served bytes match the file; the booted image runs |
| **M3** | directory browser | **done** — navigates to a file **by name**, boots it, 4096/4096 match |
| **M4** | RP2040 firmware, ESP32 board, CI | **done** — `fujivcs.uf2` builds, core1 SRAM-resident, three CI jobs |
| **M5** | `vcsmap`: the real cartridge mappers | **done** — 9 schemes vs verbatim MAME handlers, 200k fuzzed accesses each |
| **M6** | soak | **done** — 13 synthetic cartridges across all 9 schemes, every bank byte-compared: 13/13 |
| **M7** | CONFIG and fujinet-battleship | **done** — CONFIG browses and boots through a subfolder; Battleship plays a real game against the live server |

M0 was pulled forward deliberately: every sibling console had a character
generator or a framebuffer, so the cart-composed display is the one component
with no template anywhere in the family. M-novel came with it because MAME
*structurally cannot* test the write sampling — its cart device is handed a
clean data byte — so it had to be proven before anything depended on it.

M6 found two things no host test could reach. An 8K F8, E0, UA and FE are all
8192 bytes and nothing inside the file tells them apart, so size detection was
serving three of the four as F8 — the `.cfg` sibling the DBC push already
carries had to be read, as the ColecoVision port does. And UA and FE switch on
addresses **below A12**, where the cartridge is not selected at all and is only
watching the bus; neither core1 nor the MAME device was looking there.

M7 needed one protocol addition each. CONFIG needed somewhere to keep a
working directory — a path is 256 bytes and this console has 128 of RAM — so
the cartridge keeps it: `FN_HOT_PATH_CH` builds it, `FN_PATH_POP` drops a
component, and `FN_PATH_TX` emits it straight into the transaction. Battleship
needed a board, and `FN_BLIT_FIELD` had been reserved for it in
`fuji_mailbox.h` since the first commit; twelve columns turns out to be exactly
enough for a 10×10 grid plus a row-digit gutter.

## Building and running

```sh
./build.sh                      # the 6502 clients, via Macroassembler AS
./build-cart.sh                 # the RP2040 firmware, with a client baked in
make -C firmware/host_test      # test_render, test_fujibus, test_busio
./emu/apply.sh                  # graft the cart device into ~/Workspace/mame
make -C ~/Workspace/mame -j$(nproc) NOWERROR=1 REGENIE=1

./run.sh hello shot             # M0: snapshot
python3 emu/dispcheck.py build/snap/a2600/0000.png testrom/screen.txt

SLOT=fujinet ./run.sh fujitest drive       # M1, against a live fujinet-pc
SLOT=fujinet ./run.sh fujitest resettest

BOOT_IMAGE=.../SD/vcsgame.bin \
  SLOT=fujinet ./run.sh fujiboot boottest  # M2
SLOT=fujinet ./run.sh fujidir dirtest      # M3

./tools/soak.sh                            # M6: the whole mapper corpus
BOOT_IMAGE=.../SD/VCS/DEEP.BIN \
  SLOT=fujinet SECS=90 ./run.sh fujicfg cfgtest   # M7
```

Battleship lives in its own repository, next to the Arcadia and Channel F
clients: `fujinet-battleship/atari2600`, `./run.sh bsplay`.

The M2 boot target is deliberately an image with **no** `"FUJI"` claim, so the
run also proves the mailbox goes dead for a cartridge that does not claim it --
which is what has to happen when a real game boots.

**Toolchain: Macroassembler AS, not dasm.** AS assembles 6502, is already the
family's assembler for the o2 (8048), Arcadia (2650) and Channel F (F8) ports,
and CI already caches its build. `testrom/vcs.inc` is written from the hardware
register map rather than copied from the dasm world's non-redistributable
`vcs.h`.

## Things that cost real time

- **`SDL_VIDEODRIVER=dummy` is required wherever there is no `DISPLAY`.**
  Without it MAME dies with *"Could not initialize SDL No available video
  device"* even under `-video none`, because SDL comes up before the video
  backend is chosen. `run.sh` sets it.
- **MAME must run from its own tree** or `-autoboot_script` is silently ignored.
- **fujinet-pc's BoIP listener takes one client** (backlog 1). A stray MAME
  starves the next run and the symptom is a hang, not an error. `run.sh` kills
  strays first.
- **MAME's Lua binds neither `screen:hpos()` nor `cpu:total_cycles()`**, and
  `install_write_tap` did not fire on the TIA range, so raster instrumentation
  has to come from decoding snapshots.
- **`emu/apply.sh` must be re-run after every edit to a shared source.** MAME
  builds the copies, not this tree.
- **`fujins.h` is not optional.** This MAME tree already carries the Arcadia
  and Astrocade FujiNet devices; without symbol prefixing the linker picks one
  `fujimail_paint()` for the whole binary and the rest paint the wrong offsets.
- **A bankswitch hotspot returns the OLD bank's byte.** MAME installs a read
  bank and a read *tap* over the same range, and `emumem_het.cpp` runs the tap
  after the read: `data = m_next->read(...); m_tap(offset, data, mem_mask);`.
  So the access that switches the bank still returns the byte that was there
  before it, and the switch applies to the next one. This port had it inverted
  until the MAME source settled it; it is the same distinction the ColecoVision
  found between MegaCart and X-in-1, and it is invisible until a game reads its
  own hotspot for data as well as for the side effect.
- **Banking is a POINTER SWAP, never a copy.** `vcs_set_bank` runs inline in
  core1's bus loop because the switch has to be complete before the console's
  next fetch -- 838 ns, with no wait state on this bus. The first version
  `memcpy`'d 2K into the window, which is microseconds; it passed every test,
  because MAME's device has all the time in the world. Every real 2600 mapper
  swaps a pointer, and so does this one now.
- **The client's equates are hand-mirrored and WILL drift.** The spec put the
  control page at `$1D00` and `fujinet.inc` said `$1C00`; every register write
  went to a page that decodes nothing, reads of it fell through to the served
  window and returned `00`, and the client came up, drew a screen and simply
  never armed the mailbox. `tools/checkdefs.py` cross-checks all 39 equates
  against `fuji_mailbox.h` and `build.sh` runs it before assembling.
- **Equates and code go in different include files.** `fujinet.inc` is
  equates and is included before the `ORG`; `fujilib.inc` is code and is
  included inside it. Including the code half early assembles the whole
  transport at `$0000` and every `jsr` to it becomes `20 00 00` — which
  assembles cleanly and fails at run time.
