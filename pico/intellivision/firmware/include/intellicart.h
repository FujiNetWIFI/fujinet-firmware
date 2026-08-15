
#ifndef INTELLICART_H
#define INTELLICART_H

#include <stdint.h>
#include <stdbool.h>

#include "vfs.h"
#include "filesystem.h"

#define RAMSIZE   0x2800 // biggest contigus memory block useable for RAM is from 0x8000 to 0x9BFF (10kW = 20kB) based on memory map

typedef struct {
   volatile uint16_t ROM[MAX_ROM_SIZE];
   volatile uint16_t RAM[RAMSIZE];
   uint32_t len;

   bool pagingSupport;
#if CONFIG_JLP
   bool JLPSupport;
   bool JLPFlash;
   uint8_t JLPFlashSize;   // number of 1.5KB JLP flash sectors
   bool JLPAccel;
   char flashfile[512];
   vfs_file_t *filesave;

   // RAM window claimed by the bus loop ahead of mm_lookup() -- see
   // update_ram_window() and cartridge.c. $8000-$9FFF (all of it) once a
   // JLP game is loaded; otherwise, on a FujiNet-capable board, just the
   // mailbox range so game ROM can still be mapped at e.g. $9000.
   bool ramWindow;
   uint16_t ramLo, ramHi;
#endif

#if CONFIG_FUJINET
   bool FujiSupport;   // this board serves the FujiNet mailbox; set once by
                        // RunFujiConfig() and never cleared
   bool MailboxActive; // session flag: cleared when a game's map overlaps
                        // the mailbox; restored by RunFujiConfig()
#endif

#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
   bool ECSSupport;
   bool IntellivoiceSupport;
#endif
} Cartridge;

#define JLP_FEATURE_ACCEL(status)   (status > 0) // Turn on Acceleration for any jlp value <> 0 should be (status & (1U << 0))
#define JLP_FEATURE_FLASH(status)   (status > 1) // Turn on Flash for any jlp value > 1 should be (status & (1U << 1))

#define JLP_FLASH_ROWS_PER_SECTOR   8
#define JLP_FLASH_ROW_BYTES         96 * 2     // 96 * uint16_t
#define JLP_FLASH_SECTOR_BYTES      JLP_FLASH_ROWS_PER_SECTOR * JLP_FLASH_ROW_BYTES
#define JLP_RAM_ADDRESS             cart.RAM[0x25]
#define JLP_ROW_NUMBER              cart.RAM[0x26]

void init_cart(void);
int load_cfg(char *filename);
void apply_pokes(char *filename);
void config_jlp(int jlp_value, int jlpflash_value, char *filename);
int collect_info(char *filename, INFO_ENTRY *info_entries);
#if CONFIG_JLP
void update_ram_window(void);   // recompute ramLo/ramHi/ramWindow from
                                 // JLPAccel/JLPFlash/FujiSupport -- call
                                 // after changing any of those
#endif

#endif
