/* fuji_store.h -- where a DBC-pushed image's bytes live.
 *
 * Two tiers, chosen by size at OPEN time: images that fit the 32K cartridge
 * window are copied straight into the staging window and need no store at all,
 * and everything larger goes into a static RAM store. The store the console is
 * currently being served FROM cannot be overwritten, so open() refuses with
 * FN_BOOT_ERR_STOREBUSY when the only fitting store is live.
 *
 * There is deliberately NO flash tier here, unlike the Astrocade port. That
 * port serves a 1.789 MHz Z80 and can afford an XIP cache miss; this one
 * serves a 3.579545 MHz Z80 with roughly 373 ns from address-valid to
 * data-required, and an RP2040 XIP miss is ~28 QSPI clocks of line fill -- on
 * its own comparable to the entire budget. A banked image must be in SRAM or
 * it cannot be served at all, which is what caps this port at 128K images:
 * 24 of the 31 MegaCart titles in MAME's homebrew list, both SGC titles and
 * all three Activision ones fit; the 256K and 512K MegaCarts and both X-in-1
 * images do not. Lifting that ceiling is an RP2350 decision (520K of SRAM),
 * not a firmware one.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_STORE_RAM_SIZE   (128u * 1024)

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY. size 0 is
 * legal (an older peer omitting it) and lands in the staging tier. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* Commit (or abort). Returns the base of the stored image, or NULL on
 * abort/nothing. */
const uint8_t *fuji_store_close(bool aborted);

#endif /* FUJI_STORE_H */
