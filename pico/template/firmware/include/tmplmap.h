/* tmplmap.h -- the template cartridge's image mapper and connector decode. Hardware-free:
 * the firmware, the MAME device and the host tests all build it.
 */

#ifndef TMPLMAP_H
#define TMPLMAP_H

#include <stdbool.h>
#include <stdint.h>

#define TMPLMAP_WINDOW 0x2000     /* the served image: one flat 8K window */

typedef enum {
    TMPLMAP_OK = 0,
    TMPLMAP_EEMPTY,               /* zero bytes arrived              */
    TMPLMAP_ETOOBIG,              /* larger than the window          */
} tmplmap_err_t;

typedef struct {
    uint32_t size;
    bool mailbox_ok;              /* the image carries the "FUJI" claim */
} tmplmap_plan_t;

/* At stream open, before the ESP32 fetches the file: 0 to accept (0 is also "size not
 * known yet") or an FN_BOOT_ERR_*. */
uint8_t tmplmap_gate(uint32_t size);

tmplmap_err_t tmplmap_plan(const uint8_t *image, uint32_t size, tmplmap_plan_t *plan);

/* Lay the image into a window: a straight copy, 0xFF above it, as an empty ROM socket
 * reads. */
void tmplmap_apply(const uint8_t *image, const tmplmap_plan_t *plan,
                   uint8_t window[TMPLMAP_WINDOW]);

/* The window offset for the address on the connector's A0-A12. A banked or split
 * window does its mapping here, so core1 and the MAME device cannot disagree. */
static inline uint16_t tmplmap_decode(uint32_t addr)
{
    return (uint16_t)(addr & (TMPLMAP_WINDOW - 1));
}

#endif /* TMPLMAP_H */
