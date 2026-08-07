# Provenance and licensing status

`firmware/` is a vendored fork of **PiRTO II** by Andrea Ottaviani
(https://github.com/aotta/PiRTOII), forked at commit `b5e932a`. It is an RP2040-based
Intellivision cartridge that emulates ROM/RAM on the CP-1610 bus, with a USB
mass-storage-backed flash filesystem for ROM images and a menu ROM for game selection.

PiRTO II itself states in its source headers that its flash filesystem / USB-MSC layer
("parts of code are directly from the A8PicoCart project") is adapted from **A8PicoCart**
by Robin Edwards (https://github.com/robinhedwards/A8PicoCart). That derivation is
concretely traceable: `flash_fs.c` still carries A8PicoCart's on-flash filesystem magic
string `"RHE!FS30"` (Robin Edwards' initials).

## License status: unresolved — do not redistribute

**Neither upstream project carries a license.** Checked directly against both repositories
(no LICENSE file, GitHub license API returns 404, no license/copyright grant in any
first-party source header — only third-party vendored code, TinyUSB (MIT) and FatFs/ChaN
(BSD-1-clause), carries a license):

| project | author | license |
|---|---|---|
| PiRTO II | Andrea Ottaviani | **none** |
| A8PicoCart | Robin Edwards | **none** |

This is despite both authors having *other* projects that are explicitly GPLv3 (Robin
Edwards' `UnoCart-2600`; the `PlusCart-Pico` lineage already vendored in this repo under
`pico/atari-2600/`). The absence here appears to be an oversight rather than a deliberate
choice, but the default in the absence of a license is all-rights-reserved, and unlicensed
code cannot be relicensed into fujinet-firmware's GPLv3 by anyone other than its authors.

**This code is vendored here for local development and bring-up only.** It must not be
redistributed, and must not be merged upstream into fujinet-firmware, until explicit
license grants are obtained from both:

1. **Andrea Ottaviani** — for PiRTO II as a whole (`firmware/inty_cart.c`, `main.c`, and
   everything except the files below).
2. **Robin Edwards** — for the A8PicoCart-derived storage layer, which Andrea cannot grant
   on Robin's behalf: `firmware/flash_fs.c`, `firmware/flash_fs.h`, `firmware/fatfs_disk.c`,
   `firmware/fatfs_disk.h`, `firmware/msc_disk.c`.

If a grant is not obtained from Robin Edwards, the storage layer above is the bounded,
replaceable part — it can be reimplemented directly against the pico-sdk, or dropped
entirely in favor of loading ROMs solely over the FujiNet link.

The CP-1610 bus emulation engine in `firmware/inty_cart.c` — the hard, silicon-proven part
of this project, and the reason PiRTO II was forked rather than written from scratch — is
Andrea Ottaviani's original work and is the primary subject of the pending grant request.

## What we're adding

FujiNet integration (bridging the RP2040 to an ESP32-S3 running fujinet-firmware's
`fujiversal-rs232` build over USB CDC) is new code, original to this repository, and is
GPLv3 like the rest of fujinet-firmware. See `README.md` in this directory for the design.
