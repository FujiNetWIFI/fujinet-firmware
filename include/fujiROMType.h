#ifndef FUJI_ROM_TYPE_H
#define FUJI_ROM_TYPE_H

#include <stdint.h>

#define R_16K  0b10000000
#define R_8K   0b00000000
#define R_PG0  0b00000000
#define R_PG1  0b00100000
#define R_PG2  0b01000000
#define R_PG3  0b01100000

enum fujiROMType_t : uint8_t {
    ROM_TYPE_UNKNOWN          = 0x00,

    // ----------------------------------------
    // MSX ROM Types
    //
    // High 3 bits uses to indicate block size and page offset.
    // Not currently used for anything. Low 5 bits are just an arbitrary ID.
    //   0   0 0   0 0 0 0 0
    //  |_| |___| |_________|
    //   |    |        |
    //   |    |        +-- ID (0-32)
    //   |    +-- page offset
    //   +-- 16KB blocks?
    //
    // 16K Blocks
    ROM_TYPE_MSX_PLAIN_PAGE_0 = 0x01 | R_16K | R_PG0,
    ROM_TYPE_MSX_PLAIN        = 0x01 | R_16K | R_PG1,
    ROM_TYPE_MSX_PLAIN_PAGE_1 = 0x01 | R_16K | R_PG1,
    ROM_TYPE_MSX_PLAIN_PAGE_2 = 0x01 | R_16K | R_PG2,
    ROM_TYPE_MSX_PLAIN_PAGE_3 = 0x01 | R_16K | R_PG3,
    ROM_TYPE_MSX_MIRRORED     = 0x02 | R_16K | R_PG1,
    ROM_TYPE_MSX_ASCII16      = 0x03 | R_16K | R_PG1,
    // 8K Blocks
    ROM_TYPE_MSX_ASCII8       = 0x01 | R_8K  | R_PG1,
    ROM_TYPE_MSX_KONAMI       = 0x02 | R_8K  | R_PG1,
    ROM_TYPE_MSX_KONAMI_SCC   = 0x03 | R_8K  | R_PG1,
    // ----------------------------------------
};

#endif // FUJI_ROM_TYPE
