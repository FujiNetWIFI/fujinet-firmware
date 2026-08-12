#ifndef FUJICARD_H
#define FUJICARD_H

// for board detection
#define FUJICARD    1
#define BOARD_ID    6

// internal flash W25Q16JVWI
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

// see .cmake file for build options

// Pico pin usage definitions -- same bus/control pinout as pintycard (this
// board's starting point; see boards/pintycard.h). NOTE: the GPIO22
// FUJI_SELFTEST_TRIGGER / GPIO28 UART-debug pins documented in
// ~/Workspace/PiRTOII-Fuji/HARDWARE.md do NOT apply here -- that pin map
// was for the original PiRTO II-derived pinout (BDIR on GPIO16), which
// this board does not use (BDIR is GPIO22 here, inherited from pintycard).
// HARDWARE.md's own PCB was never taken past schematic planning, so there
// is no committed physical pin map to match yet; the assignments below use
// whatever pintycard leaves free.
#define MI_AD0_PIN            0

#define MI_RST_PIN            20
#define MI_LED_PIN            25

#define MI_MSYNC_PIN          21
#define MI_BDIR_PIN            22
#define MI_BC1_PIN            26
#define MI_BC2_PIN            27

// UART (debug console, Debug builds only -- see include/board.h)
#define MI_DBG_UART_ID        1
#define MI_DBG_UART_RX_PIN    -1    // TX only
#define MI_DBG_UART_TX_PIN    24

// FujiNet self-test trigger: ground at power-on to run fujibus_selftest()
// instead of the normal boot. Free on pintycard's pinout (no SD -> SPI0
// pins 16-19 are also free, but this uses the pin HARDWARE.md's design
// intent (a low GPIO count, not shared with SPI) most directly maps to).
#define FUJI_SELFTEST_TRIGGER_PIN  23

#endif
