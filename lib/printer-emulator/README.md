# lib/printer-emulator

Turns the byte stream a computer sends to its printer into a PDF, SVG, PNG, HTML or text file on
the SD card, which the web UI then offers for download.

## Layout
| File | Defines |
|---|---|
| `printer_emulator.h`, `printer_emulator.cpp` | `printer_emu`, the abstract base: `paper_t` output kinds (RAW, TRIM, ASCII, PDF, SVG, PNG, HTML, HTML_ATASCII), the output `FileSystem`, EOL and Atari 850 translation settings; subclasses implement `post_new_file`, `pre_close_file`, `process_buffer` and `modelname` |
| `pdf_printer.h`, `pdf_printer.cpp` | `pdfPrinter`: PDF page and font handling; loads each model's `LUT` and `F1..Fn` font files from the flash filesystem |
| `svg_plotter.h`, `svg_plotter.cpp` | `svgPlotter`: vector output for plotters |
| `png_printer.h`, `png_printer.cpp` | `pngPrinter`: bitmap output (a rewrite of TinyPngOut) |
| `file_printer.h`, `file_printer.cpp`; `html_printer.h`, `html_printer.cpp` | `filePrinter` (RAW, TRIM, ASCII) and `htmlPrinter` (HTML, HTML_ATASCII) |
| `atari_820`, `atari_822`, `atari_825`, `atari_1025`, `atari_1027`, `atari_1029`, `atari_xdm121` | Atari models on `pdfPrinter` |
| `atari_1020` | `atari1020` on `svgPlotter` |
| `epson_80`, `epson_tps.h`, `atari_xmm801` | `epson80` on `pdfPrinter`; `epsonTPS` and `xmm801` derive from it |
| `okimate_10` | `okimate10`, derived from `atari1025` |
| `coleco_printer`, `commodoremps803` | `colecoprinter` (with `adamBidiBuffer`) and `commodoremps803` on `pdfPrinter` |

## How it fits
- Each bus's printer device under [lib/device/](../device/) instantiates the model the user picked
  and feeds it bus data; the printer list there owns the slots.
- Output goes to `fnSDFAT` from [lib/FileSystem/](../FileSystem/). The font files are
  built into the flash image from `data/webui/common/f/`.

## Build
ESP: globbed into every target. PC: every `FUJINET_TARGET` builds the base classes and the Atari,
Epson and Okimate models; `coleco_printer` is added for ADAM and LYNX; `commodoremps803` (IEC only)
is not compiled on PC.
