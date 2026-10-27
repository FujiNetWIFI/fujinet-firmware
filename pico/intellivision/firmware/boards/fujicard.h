#ifndef FUJICARD_H
#define FUJICARD_H

// for board detection
#define FUJICARD    1
#define BOARD_ID    6

// internal flash W25Q16JVWI
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

// see .cmake file for build options

// Bus/control pinout as pintycard; extra pins use what pintycard leaves free.
// No PCB pin map is committed yet.
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

// Ground at power-on to run fujibus_selftest() instead of booting.
#define FUJI_SELFTEST_TRIGGER_PIN  23

#endif
