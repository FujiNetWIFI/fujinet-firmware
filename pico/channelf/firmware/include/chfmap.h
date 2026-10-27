/* chfmap.h -- Channel F cartridge image gate, plan, apply and claim test.
 *
 * Every commercial Videocart is a flat 2K-6K ROM at $0800; the ones with extra
 * hardware (Videocart 10 and 18's 2102 on the PSU ports, Saba Schach's 3853
 * with 2K RAM at $2800) add memory, not banking. Hardware-free: shared by the
 * cart firmware, the MAME device and the host tests.
 */

#ifndef CHFMAP_H
#define CHFMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

#define CHFMAP_WINDOW FN_ROM_SIZE      /* the fixed ROM window: 16K at $0800 */
#define CHFMAP_STORE  FN_APP_MAX_SIZE  /* one slot: the largest banked image  */

typedef enum {
    CHFMAP_OK = 0,
    CHFMAP_EEMPTY,   /* zero bytes: nothing arrived        */
    CHFMAP_ETOOBIG,  /* over 16K and not 16K + whole pages */
    CHFMAP_ENOSIG,   /* no $55 at $0800: the BIOS would ignore it */
    CHFMAP_ENOCLAIM, /* pages but no claim: nothing could ever select them */
} chfmap_err_t;

typedef struct {
    uint32_t size;
    bool mailbox_ok; /* image carries the "FUJI" claim at FN_ROM_CLAIM */
    uint8_t npages;  /* APPBANK pages after the fixed 16K; 0 = flat image */
} chfmap_plan_t;

/* 16K + k whole 8K pages, 1 <= k <= FN_APP_MAX_PAGES. */
bool chfmap_app_shaped(uint32_t size);

/* Size gate at stream open, before the ESP32 sends the file; 0 to accept
 * (size 0 = still unknown) or an FN_BOOT_ERR_*. */
uint8_t chfmap_gate(uint32_t size);

chfmap_err_t chfmap_plan(const uint8_t *image, uint32_t size,
                         chfmap_plan_t *plan);

/* Linear copy into the slot; a flat image is $FF-filled to 16K, matching
 * MAME's stock chanf_rom_device. `image` may be the slot itself. */
void chfmap_apply(const uint8_t *image, const chfmap_plan_t *plan,
                  uint8_t store[CHFMAP_STORE]);

/* Only an exactly-16K or banked image can carry the FN_ROM_CLAIM signature. */
bool chfmap_claims(const uint8_t *image, uint32_t size);

#endif /* CHFMAP_H */
