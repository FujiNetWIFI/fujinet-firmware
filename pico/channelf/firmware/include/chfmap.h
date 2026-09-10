/* chfmap.h -- Channel F cartridge image mapping.
 *
 * The Astrocade port needed a real mapper and the ColecoVision needed five of
 * them. This platform needs almost none: every commercial Videocart is a flat
 * 2K-6K ROM starting at $0800, and the ones with extra hardware (Videocart 10
 * and 18 hang a 2102 off the PSU I/O ports; Saba Schach uses a 3853 with 2K of
 * static RAM at $2800) add memory rather than banking. Nothing switches banks.
 *
 * So what is shared between the RP2040 firmware and the MAME cart device is
 * just the image gate, the plan and the apply -- plus the claim test, which is
 * what decides whether the mailbox survives a boot.
 *
 * Hardware-free by design: compiled into the cartridge firmware, the MAME
 * device, and the host tests.
 */

#ifndef CHFMAP_H
#define CHFMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

#define CHFMAP_WINDOW FN_ROM_SIZE /* the served ROM window: 16K at $0800 */

typedef enum {
    CHFMAP_OK = 0,
    CHFMAP_EEMPTY,  /* zero bytes: nothing arrived        */
    CHFMAP_ETOOBIG, /* over 16K: not mappable here        */
    CHFMAP_ENOSIG,  /* no $55 at $0800: the BIOS would ignore it */
} chfmap_err_t;

typedef struct {
    uint32_t size;
    bool mailbox_ok; /* image carries the "FUJI" claim at FN_ROM_CLAIM */
} chfmap_plan_t;

/* Size gate at stream-open time, before the ESP32 drags the file over the
 * network; 0 to accept (size 0 = still unknown) or an FN_BOOT_ERR_*. */
uint8_t chfmap_gate(uint32_t size);

chfmap_err_t chfmap_plan(const uint8_t *image, uint32_t size,
                         chfmap_plan_t *plan);

/* Lay the image into a served window: linear copy, $FF fill above -- which is
 * byte-for-byte what MAME's stock chanf_rom_device does (read_rom returns 0xff
 * past m_rom_size), so a soak run can compare the two directly. */
void chfmap_apply(const uint8_t *image, const chfmap_plan_t *plan,
                  uint8_t window[CHFMAP_WINDOW]);

/* Does this image promise to keep the mailbox alive? See FN_ROM_CLAIM: only an
 * exactly-16K image can carry the signature, because a shorter one does not
 * reach $47FC and past its end is open bus. */
bool chfmap_claims(const uint8_t *image, uint32_t size);

#endif /* CHFMAP_H */
