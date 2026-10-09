/* a52map_db.h -- known images, keyed by CRC-32: MAME's software list, plus
 * the No-Intro images it lacks (tools/mkmapdb.py). */

#ifndef A52MAP_DB_H
#define A52MAP_DB_H

#include <stdint.h>

#include "a52map.h"

typedef struct a52map_db {
    uint32_t crc;
    uint8_t  kind;                  /* A52MAP_*                             */
} a52map_db_t;

extern const a52map_db_t a52map_db[];       /* sorted by crc */
extern const unsigned a52map_db_count;

#endif /* A52MAP_DB_H */
