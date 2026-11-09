#ifndef JLPFLASH_H
#define JLPFLASH_H

#include <stdbool.h>
#include <stdint.h>

#define JLP_FLASH_ROW_WORDS         96
#define JLP_FLASH_ROWS_PER_SECTOR   8
#define JLP_FLASH_ROW_BYTES         (JLP_FLASH_ROW_WORDS * 2)
#define JLP_FLASH_SECTOR_BYTES      (JLP_FLASH_ROWS_PER_SECTOR * JLP_FLASH_ROW_BYTES)
#define JLP_FLASH_RAM_LO            0x8040  // JLP RAM a row may be copied to or from
#define JLP_FLASH_RAM_HI            0x9F7F

// First row of the sector that holds row.
static inline int jlp_sector_first_row(int row)
{
   return row - row % JLP_FLASH_ROWS_PER_SECTOR;
}

static inline bool jlp_row_ok(int row, int num_rows)
{
   return row >= 0 && row < num_rows;
}

// A row's RAM span must lie in JLP RAM and miss the mailbox when one is
// mapped there (mbx_lo > mbx_hi: none).
static inline bool jlp_span_ok(uint32_t addr, uint32_t mbx_lo, uint32_t mbx_hi)
{
   uint32_t last = addr + JLP_FLASH_ROW_WORDS - 1;

   if (addr < JLP_FLASH_RAM_LO || last > JLP_FLASH_RAM_HI)
      return false;
   return mbx_lo > mbx_hi || last < mbx_lo || addr > mbx_hi;
}

void readFlash(int row, uint16_t addr);
void writeFlash(int row, uint16_t addr);
void eraseFlash(int row);

#endif
