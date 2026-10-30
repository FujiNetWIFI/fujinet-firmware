/* fuji_store.h -- where a pushed image waits for the swap. Coleco keeps its own store, in
 * place of pico/common's: an image up to the 32K window goes straight into the staging
 * window, a bigger one into a 128K RAM store.
 *
 * No flash tier: at 3.58 MHz an M1 fetch leaves ~373 ns, and an XIP cache miss is most of
 * that, so a banked image must be served from SRAM. That caps images at 128K; the 256K and
 * 512K MegaCarts and the X-in-1 images need an RP2350.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_STORE_RAM_SIZE   (128u * 1024)

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY (the RAM store is being
 * served from). Size 0 (an older peer) lands in the staging window. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* The image's base, or NULL on abort, nothing, or a push that closed short of OPEN's size.
 * *kept, if given, is how much the store holds: less than written when an image with no
 * size at OPEN outgrew the window. */
const uint8_t *fuji_store_close(bool aborted, uint32_t *kept);

#endif /* FUJI_STORE_H */
