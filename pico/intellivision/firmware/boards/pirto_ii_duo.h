#ifndef PIRTO_II_DUO_H
#define PIRTO_II_DUO_H

// PirtoII Duo with microSD slot by Aotta (https://github.com/aotta/PiRTOIIDuo)

// for board detection
#define PIRTO_II_DUO 1
#define BOARD_ID     4

#define PICO_RP2350A 1     // 1 for RP2350A, 0 for RP2350B

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#define PICO_RP2350_A2_SUPPORTED 1

// see .cmake file for build options

// Pico pin usage definitions
#define MI_AD0_PIN   0

#define MI_RST_PIN   20
#define MI_LED_PIN   25

#define MI_MSYNC_PIN 21
#define MI_BDIR_PIN  22
#define MI_BC1_PIN   26
#define MI_BC2_PIN   27
                     
// SD
#define MI_SD_SPI          0
#define MI_SD_SPI_RX_PIN   16
#define MI_SD_SPI_CSN_PIN  17
#define MI_SD_SPI_SCK_PIN  18
#define MI_SD_SPI_TX_PIN   19

#if (CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE)
// ECS AUDIO AY-3-8910
#define MI_AUDIO_PIN       28
#else
// UART
#define MI_DBG_UART_ID        0
#define MI_DBG_UART_RX_PIN    -1    // TX only
#define MI_DBG_UART_TX_PIN    28
#endif

#endif
