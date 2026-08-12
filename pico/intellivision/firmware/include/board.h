#ifndef BOARD_H
#define BOARD_H

#define AD0          MI_AD0_PIN
#define ADWIDTH      16

#define RESET        MI_RST_PIN
#define LED          MI_LED_PIN
#define MSYNC        MI_MSYNC_PIN
#define BDIR         MI_BDIR_PIN
#define BC1          MI_BC1_PIN
#define BC2          MI_BC2_PIN
#ifdef MI_DIR_PIN
   #define DIRC      MI_DIR_PIN
#endif

#if CONFIG_SD_STORAGE
   #define SD_SPI_PORT  SPI_INSTANCE(MI_SD_SPI)
   #define SD_SCK       MI_SD_SPI_SCK_PIN
   #define SD_MOSI      MI_SD_SPI_TX_PIN
   #define SD_MISO      MI_SD_SPI_RX_PIN
   #define SD_CS        MI_SD_SPI_CSN_PIN
#endif

#ifndef NDEBUG
   #ifdef MI_DBG_UART_ID
      #define UART_ID   uart_get_instance(MI_DBG_UART_ID)
      #define UART_TX   MI_DBG_UART_TX_PIN 
      #define UART_RX   MI_DBG_UART_RX_PIN 
   #endif
#endif

#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
   #define AUDIO_PIN    MI_AUDIO_PIN
#endif

#endif
