# lib/printer-emulator

Turns the byte stream a computer sends to its printer into a PDF, SVG, PNG, HTML or text file on
the SD card, which the web UI then offers for download.

## How it fits
- Each bus's printer device under [lib/device/](../device/) instantiates the model the user picked
  and feeds it bus data; the printer list there owns the slots.
- Output goes to `fnSDFAT` from [lib/FileSystem/](../FileSystem/). The font files are
  built into the flash image from `data/webui/common/f/`.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET` builds the base classes and the Atari,
Epson and Okimate models; `coleco_printer` is added for ADAM and LYNX; `commodoremps803` (IEC only)
is not compiled on PC.

## Notes
- `png_printer.cpp` is a rewrite of TinyPngOut.
