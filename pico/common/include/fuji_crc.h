/* fuji_crc.h -- CRC-32 (zlib's), and a lookup in a table keyed by it. The MAME grafts
 * build this too, so a known image maps the same way in both.
 */

#ifndef FUJI_CRC_H
#define FUJI_CRC_H

#include <stddef.h>
#include <stdint.h>

uint32_t fuji_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

/* The row of db[0..n) whose leading uint32_t is crc, or NULL. Rows sit stride bytes
 * apart, sorted by crc, as each cart's mkmapdb.py writes them. */
const void *fuji_crc_find(const void *db, unsigned n, size_t stride, uint32_t crc);

#endif /* FUJI_CRC_H */
