# FujiNet for the Fairchild Channel F

Fifth cart-mailbox bring-up, after Intellivision, Odyssey&sup2;, Astrocade,
Arcadia 2001 and ColecoVision. Branch `channelf-bringup`, based on
`colecovision-bringup`.

## Why this console is not the cramped one

The Channel F looks like the hardest target in the family — 64 bytes of CPU
scratchpad, no RAM anywhere in the address space, 1976 — and it is the easiest.

Astrocade, Arcadia and ColecoVision all push both mailbox directions through
the *read* path, because none of those cart edges carries a write strobe. That
is why the Arcadia client lives in 84 bytes of RAM and the O2 client in 39.

A Channel F cart is not a ROM. It is a peer on the F8 bus that services read
**and** write cycles (`ROMC 05` is a store), and it owns `$0800-$FFFF`
outright. So it can hand the console 30K of genuine read/write RAM, and the
four strings that dominate every previous CONFIG port's RAM ledger stop being a
problem. Real hardware did this: the Saba Schach cart carried 2K of 2114s.

The cost lands elsewhere: **the F8 has no address bus.** Five ROMC lines encode
a 32-state protocol, and every memory device keeps its own PC0/PC1/DC0/DC1,
updating them on *every* cycle. The cart is a state machine, and one missed
cycle corrupts its shadow registers permanently.

## Status

| | | |
|---|---|---|
| M0 | toolchain, cart header, text renderer | **done** |
| M5 | ROMC conformance vs MAME's F8 core | **done** |
| M1 | mailbox round trip + reset survival | **done** |
| M2 | network boot, byte-identical | **done** |
| M3 | directory browser: cursor, descend, boot | **done** |
| M4 | RP2040 firmware, core1 SRAM-resident | **done** |
| M6 | soak: 20/20 images pushed, booted, byte-compared | **done** |
| M7 | full CONFIG + 5 Card Stud | **done** |

M5 was pulled ahead of the rest deliberately. It is the only genuinely novel
component and the only one with no template in the sibling ports, so it is the
thing worth failing early.

The two applications live in their own repositories:
`fujinet-config/channelf` (5,875 bytes, Astrocade parity) and
`fujinet-5cardstud/channelf` (4,767 bytes). Both are driven headless from here
-- `emu/cfgdrive.lua` takes CONFIG from power-on to a byte-identical booted
cartridge, and `emu/5carddrive.lua` types a name, lists the real tables and
renders a live seven-player hand.

Not done: hardware. That was the agreed stopping point, and the bus **timing**
is the one thing emulation cannot settle -- see below.

## The cartridge firmware

`./build-cart.sh` produces `firmware/build-fujichannelf/fujichannelf.uf2`.
core1 runs the F8 bus loop out of SRAM (`channelf_core1_main` at `0x200000c0`,
which CI greps the linker map for); core0 does USB and the mailbox service.

Residency matters here for a different reason than on the ColecoVision. There
it was latency: 373 ns from address-valid to data-required, and one XIP cache
miss is most of that. Here a bus cycle is 2.2 us and latency is not the
problem -- but with **no address bus**, a cart that stalls for one cycle has
nothing to resynchronise its shadow PC0/DC0 against, and is wrong for the rest
of the session. The loop may never miss a cycle, so it may never fault to
flash.

Memory: two 16K ROM windows (ping-ponged, so a booted client can push again
without overwriting the code it is executing from) plus one 32K arena, 16K of
push buffer and the ring -- 133K of the RP2040's 264K. No image store, no flash
tier: every Videocart ever made is 6K or less.

The bus **timing** is the one thing that is provisional and cannot be settled
without a board: which edge of WRITE marks a cycle, and when the CPU's write
data is valid. The ROMC *decode* is proven against MAME's own F8 core; the
edges are marked PROVISIONAL in `channelf_cart.c` and are the first thing to
put a scope on.

## Verification

```
cd firmware/host_test && make          # test_busio + test_chfmap + test_romc
./build.sh hello romctest              # console clients
tools/mktrace.sh ~/Workspace/mame      # regenerate golden ROMC traces
```

`test_romc` replays traces captured from MAME's own F8 core and checks two
independent things per cycle: that the cart's shadow registers match the CPU's,
and that its decision to drive the bus matches a predicate written separately
from the F8 spec wording. The second check exists because the first has a blind
spot — a cart that wrongly declines to drive would fall back to the trace's own
bus value, which is the correct byte, and the registers would still agree.

Current: **1,132,507 cycles, 27 of 32 ROMC states, zero divergence.** The five
uncovered states are unreachable rather than untested: `0F`/`10`/`13` need a
device asserting `/INTREQ` and the console wires no interrupt source, and
`1E`/`1F` are emitted by no F8 instruction at all. A 30-mutation sweep against
the observer is caught 30/30.

`test_busio` covers what no trace can: the RAM data path, the mailbox write
decode and the I/O port latch, none of which exists on the standard Videocart
MAME runs. `test_chfmap` checks the image mapper against an expectation written
out separately from the implementation, plus a fuzz over every size.

`tools/soak.sh` pushes a corpus over the network, boots each image and compares
the whole served window against the file on disk -- including that everything
past the image reads `$FF`, which is what MAME's own stock Videocart device
answers past its ROM size. **20/20**, from a 1-byte image to a full 16K window.
There is no Channel F ROM set on this machine (MAME ships only the BIOS), so
`tools/mkcorpus.py` generates the images; the sizes are every real Videocart
size plus every boundary, and two full-window images differing only in the
claim confirm that one keeps the mailbox alive across the boot and the other
does not.

## Platform facts, each verified against a source rather than recalled

**Cart connector, 22 pins** — `GND`x2, `D0-D7`, `ROMC0-4`, `PHI`(13),
`WRITE`(15), `/INTREQ`(5), `+5V`x2, `NC`(21), `+12V`(22). 16 signals to the
RP2040; leave +12V unconnected. There is **no RESET pin** — a console reset is
seen on the bus as `ROMC 08`, which `f8.cpp`'s `device_reset()` issues before
the first fetch. Every sibling port had to synthesise that signal some other
way; here it arrives free.

**Timing** — 3.579545 MHz colourburst / 2 = 1.7897725 MHz. Cycles are 4 PHI
periods (short, **2.235 us**) or 6 (long, **3.353 us**), against ColecoVision's
373 ns. Correctness-bound, not latency-bound. No PIO, no overclock.

**Cart entry** — the BIOS compares the byte at `$0800` against `$55` and jumps
to `$0802`; `$0801` is skipped. Disassembled from `sl31253`/`sl31254` at
`$000F`: `DCI $0800` / `LM` / `CI $55` / `BNZ` / `JMP $0802`. It also zeroes all
64 scratchpad registers and sets r59 = 40, the K-stack pointer, so BIOS calls
are safe from the moment the cart is entered.

**Plotting a pixel** — transcribed from the BIOS's own plotter at `$0718-$0765`,
not from documentation, and two details are easy to get wrong:

- port 5 carries the row in bits 5-0 *and the sound bits in 7-6*. The BIOS does
  a read-modify-write (`INS 5` / `NI $C0` / `AS`) to keep them. Forcing them to
  zero silences whatever tone the client is playing.
- the strobe is `$60` then `$50` on port 0, followed by a 4-iteration settling
  delay. MAME only tests bit 5, so a wrong sequence passes in emulation and
  drops pixels on hardware.

Column and row both go out complemented; the colour byte is complemented too,
which makes the VRAM pixel value exactly `r3 >> 6`.

**Colour is a value, not a colour.** A pixel is 2 bits, and what those bits show
depends on the row's palette, chosen by the pixel values in columns 125 and 126
of that row — only bit 1 of each matters. Palette 0 is black with all three
non-zero values white; palettes 1-3 give BLUE/RED/GREEN on a light ground. The
`ves.h` names `COLOR_GREEN`/`RED`/`BLUE` describe palette 3 only and are
misleading everywhere else, so this port names them `CVAL0`-`CVAL3`.
`clrscrn` paints columns 125/126 too, so `DPAL` has to run *after* it.

**Text** — the BIOS font at `$0767` holds digits plus G, M, T, X and some
punctuation. Useless for filenames, so the port ships its own: a 3x5 glyph in a
4x6 cell, `$20-$5F`, 320 bytes, generated by `tools/mkfont.py`. Over the ~95x58
safe area that is **23 columns x 9 rows** — within a row and a column of the
Astrocade CONFIG's proven 26x10 layout.

**`PI` and `JMP` clobber the accumulator.** Both load the target address's
high byte into A as scratch storage (`m_a = m_dbus` in MAME's `f8_pi`), so **A
does not survive a call**. Nothing may take an argument in A across one --
`DHEXS` takes its byte in r0 for exactly this reason. Returning a value in A is
fine, because `POP` does not touch it; so are `PK`, `LR P,K` and `LR P0,Q`.
This cost real time: the first M1 run printed `SEQ 09` instead of `SEQ 01`,
where `09` was the high byte of the callee's own address.

**The F8 keeps one return address.** `PI` copies PC0 into PC1 and `POP` restores
it, so nesting needs PC1 saved by hand through the K register. The BIOS
`push_k`/`pop_k` pair does that into r40-r58, about nine levels, at the cost of
a third of the scratchpad. `DSTR` sidesteps it entirely by inlining the glyph
walk and the pixel write rather than calling them. The CONFIG client will want
its own deeper stack through DC1 instead.

**Scratchpad** — 64 registers, but only r0-r11 are directly addressable; the
rest go through ISAR. BIOS reserves r5-r8, r31, r40-r58, r59, r52-r54.

**A drawing primitive eats most of the register file.** `DCLRR` clobbers r0,
r2 and r4-r8, so a loop that calls it cannot keep its counter in a register --
an early `SCUR` kept one in r7 and never terminated. Loop state goes in RAM;
this is the one console in the family where that costs nothing.

**Point DC0 at the TX page in the helper, not the caller.** A write anywhere in
that page appends, so re-pointing is free and cannot rewind the stream. Without
it, any `DCI` between `FNBEG` and a parameter append silently redirects the
parameter bytes into RAM -- the command then goes out with no parameters at all
*and* scribbles over whatever the pointer was aimed at, which is how a page
counter and a "more entries" flag got corrupted at the same time.

**Do not seek to directory position 0.** A freshly opened directory is already
there, and `dir_seek(0)` fails outright on an empty one (`fnFsSD.cpp` requires
`pos < _dir_entries.size()`), which reads as an error rather than as the empty
listing it is. Seeking once per page instead of once per row also costs one
round trip per page rather than seven.

## Toolchain

Macroassembler AS with `CPU F3850` — the same assembler the Arcadia (2650) and
O2 (8048) ports already use, so CI reuses their cache block unchanged. Verified
against real Channel F idioms: `ST`=`17`, `LR H,DC`=`11`, `LR Q,DC`=`0E`,
`LR DC,H`=`10`, `LR DC,Q`=`0F`, `LR K,P`=`08`, `LR P,K`=`09`, `XDC`=`2C`.

## Licence note

[`ZX-80/PicoVideocart`](https://github.com/ZX-80/PicoVideocart), an RP2040
Channel F flashcart, is **unlicensed** — the GitHub API reports
`"license": null` and there is no LICENSE file. This is the third time in this
family, after PicoPAC and PiRTO II. Treat it as an electrical reference only:
no code, no schematics. Everything protocol-related here comes from MAME's F8
core (BSD-3-Clause) and the F8 User's Guide instead.
