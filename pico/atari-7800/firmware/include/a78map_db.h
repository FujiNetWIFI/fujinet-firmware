/* a78map_db.h -- known images, keyed by CRC-32, from MAME's software list. */

#ifndef A78MAP_DB_H
#define A78MAP_DB_H

#include <stdint.h>

#include "a78map.h"

#define A78DB_SWAP8K 0x01           /* No-Intro Activision layout           */

typedef struct a78map_db {
    uint32_t crc;
    uint8_t  kind;                  /* A78MAP_*, or A78MAP_UNSUPPORTED      */
    uint8_t  flags;                 /* A78DB_*                              */
    uint8_t  biosok;                /* A78_BIOSOK_*: measured by biosok.py  */
    uint8_t  ram_kb;                /* cart RAM on the board, 0 if unknown  */
} a78map_db_t;

extern const a78map_db_t a78map_db[];       /* sorted by crc */
extern const unsigned a78map_db_count;

#endif /* A78MAP_DB_H */
