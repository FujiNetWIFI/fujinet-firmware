#ifndef PIRTO_II_SD_H
#define PIRTO_II_SD_H

// PirtoII with microSD slot by sukkopera (https://github.com/SukkoPera/PiRTOII)

// for board detection
#define PIRTO_II_SD     1
#define BOARD_ID        3

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
 
// see .cmake file for build options

// Pico pin usage definitions
#define MI_AD0_PIN   0

#define MI_RST_PIN   20
#define MI_LED_PIN   25

#define MI_MSYNC_PIN 21
#define MI_BDIR_PIN  22
#define MI_BC1_PIN   26
#define MI_BC2_PIN   27
#define MI_DIR_PIN   28 // HIGH= A->B (INPUT 5V TO 3.3V, inty -> pico) LOW= B->A (OUTPUT 3.3 TO 5V, pico -> inty)
                     
// SD
#define MI_SD_SPI          0
#define MI_SD_SPI_RX_PIN   16
#define MI_SD_SPI_CSN_PIN  17
#define MI_SD_SPI_SCK_PIN  18
#define MI_SD_SPI_TX_PIN   19

#endif
