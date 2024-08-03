#ifndef SD_H
#define SD_H

#include <stdint.h>
#include "global.h"
#include "menu.h"
#include "board.h"
#include "FatFsSd.h"
#include "ffconf.h"

enum sd_card_stat {
   sd_card_total_size,
   sd_card_used_size,
};

static spi_t spi = {
   .hw_inst = spi0,  // RP2040 SPI component
   .miso_gpio = SD_MISO,
   .mosi_gpio = SD_MOSI,
   .sck_gpio = SD_SCK,
   // 2 MHz OK
   .baud_rate = SD_SPEED,
   .no_miso_gpio_pull_up = true
};

static sd_spi_if_t spi_if = {
   .spi = &spi,  // Pointer to the SPI driving this card
   .ss_gpio = SD_CS      // The SPI slave select GPIO for this SD card
} ;

// Hardware Configuration of the SD Card "objects"
static sd_card_t sd_card = {
   .type = SD_IF_SPI,
   .spi_if_p = &spi_if,  // Pointer to the SPI interface driving this card
   .use_card_detect = false,
};

bool is_text_file(char *);
FRESULT recursive_search(char *, char *, MENU_ENTRY **, int *, FIL*);
bool sd_card_file_list(char *, MENU_ENTRY **, int *);
uint32_t sd_card_file_request(uint8_t *, char *, uint32_t, uint32_t);
bool sd_card_find_file(char *, char *, MENU_ENTRY **, int *);
int sd_card_file_size(char *);
int *sd_card_statistics(void);

#endif	/* SD_H */
