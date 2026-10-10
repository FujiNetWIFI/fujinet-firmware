/* smsmap_db.h -- known images, keyed by CRC-32, from MAME's software list. */

#ifndef SMSMAP_DB_H
#define SMSMAP_DB_H

#include <stdint.h>

#include "smsmap.h"

typedef struct smsmap_db {
    uint32_t crc;
    uint8_t  kind;            /* SMSMAP_*, or SMSMAP_UNSUPPORTED             */
    uint8_t  ram_kb;          /* cart RAM, KiB                               */
} smsmap_db_t;

extern const smsmap_db_t smsmap_db[];       /* sorted by crc */
extern const unsigned smsmap_db_count;

#endif /* SMSMAP_DB_H */
