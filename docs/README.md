# docs

Design and procedure notes too long for a code comment. The contributor rules live at the
repository root in `CONTRIBUTING.md` and `AGENTS.md`.

## Layout
| File | Subject |
|---|---|
| `cpp-style.md` | C++ and code-structure rules; the companion to `CONTRIBUTING.md` that new device code is reviewed against |
| `fujiversal-flashing.md` | How one ESP32 firmware image carries and flashes the companion RP2040/RP2350 cartridge firmware over PICOBOOT |
| `mac68k.md` | The Macintosh 68k board: ESP32 plus Pico on the floppy port, disk slots and image types |
| `mac68k-floppy-write.md` | The writable-floppy (GCR capture) path on that board, with its test plan |
| `mac68k-stuffit.md` | Mounting StuffIt, BinHex and MacBinary archives on the Mac target, and the sample corpus |
| `build-sh-readme.md` | A second description of `build.sh`. The root `build-sh.md` is the one `README.md` and `build.sh` point to; prefer it |

## How it fits
- [build-platforms/](../build-platforms/), [data/webui/](../data/webui/) and the per-directory
  READMEs under [lib/](../lib/) carry their own documentation. [notes/](../notes/) holds a single
  wiring note for the CoCo DevKitC board.
