/* hsc.h -- the High Score Cart's battery RAM, kept on the FujiNet's SD card.
 *
 * core1 mirrors every console write to $1000-$17FF into a 2K shadow and
 * marks its 64-byte chunk dirty. Here the shadow is restored once the ESP32
 * link is up, and dirty chunks are saved while a game runs, two seconds after
 * the last write. Each chunk is one AppKey (OPEN, then a 64-byte WRITE or a
 * READ), so 32 keys hold the whole RAM. A key that reads back empty fills
 * with $FF, as MAME's HSC starts; the HSC formats whatever it does not
 * recognise.
 *
 * Shared by the cart and the MAME device; the port supplies the link.
 */

#ifndef HSC_H
#define HSC_H

#include <stdbool.h>
#include <stdint.h>

#include "fujibus.h"

#define HSC_CHUNK        64
#define HSC_CHUNKS       32
#define HSC_DEBOUNCE_MS  2000u

/* Provisional AppKey identity; to be registered on the FujiNet wiki. */
#define HSC_CREATOR      0x7800u
#define HSC_APP          0x01u

#define FUJI_DEVICE_ID   0x70u
#define CMD_APPKEY_OPEN  0xDCu
#define CMD_APPKEY_READ  0xDDu
#define CMD_APPKEY_WRITE 0xDEu

typedef struct {
    fb_status_t (*transact)(uint8_t device, uint8_t command,
                            const fb_param_t *params, unsigned nparams,
                            const uint8_t *payload, uint16_t payload_len,
                            uint32_t timeout_ms, fb_reply_t *reply);
    bool (*link_up)(void);
    uint32_t (*now_ms)(void);
} hsc_port_t;

typedef struct {
    const hsc_port_t *port;
    uint8_t *ram;                 /* the 2K shadow                          */
    volatile uint32_t *dirty;     /* core1's chunk bits                     */
    uint32_t pending;             /* taken from *dirty, not yet saved       */
    uint32_t last_change_ms;
    uint32_t seen;                /* *dirty as of the last look             */
    bool restored;
    bool sd_ok;                   /* the last AppKey call succeeded         */
} hsc_t;

void hsc_init(hsc_t *h, const hsc_port_t *port, uint8_t *ram, volatile uint32_t *dirty);

/* Read all 32 keys into the shadow. Returns true if the link carried it. */
bool hsc_restore(hsc_t *h);

/* Call from the main loop. `idle` means nothing else owns the link: a game is
 * running, the mailbox off. */
void hsc_service(hsc_t *h, bool idle);

/* Save one chunk now; true on success. */
bool hsc_save_chunk(hsc_t *h, unsigned chunk);

#endif /* HSC_H */
