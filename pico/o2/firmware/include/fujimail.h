/* fujimail.h -- the mailbox service: register decode, the SEQ/ACKSEQ interlock,
 * and the DBC ROM-push receiver.
 *
 * Shared by the RP2040 cartridge and the o2em model, behind a small port
 * interface, for the same reason fujibus.c and o2map.c are shared: this is the
 * protocol, and two copies of a protocol drift. Because the emulator drives
 * this code, every browse-and-boot run against a real fujinet-pc-rs232
 * exercises what the cartridge actually ships rather than a lookalike.
 *
 * The port supplies the four things that genuinely differ between a cartridge
 * and an emulator: where a published byte goes, how a frame reaches the
 * ESP32-S3, what to do with a finished push stream, and how to reboot into
 * BOOTSEL.
 */

#ifndef FUJIMAIL_H
#define FUJIMAIL_H

#include <stdbool.h>
#include <stdint.h>

#include "fujibus.h"
#include "o2map.h"

/* The largest layout o2map will accept: 8 banks of 3K. Sizing this by guess
 * would silently truncate a maximum-size image and boot the remains. */
#define FUJIMAIL_STAGE_MAX (O2MAP_BANKS * 3072)
#define FUJIMAIL_RX_MAX    1024

/* Reported to the port purely so it can log; nothing depends on it. */
typedef struct {
    uint8_t device, command, nparam, seq, reply_cmd;
    uint16_t txlen, rxlen;
    int status;                 /* fb_status_t */
    const uint8_t *rx;
} fujimail_txn_t;

typedef enum {
    FUJIMAIL_DBC_OPEN,
    FUJIMAIL_DBC_CLOSE,
} fujimail_dbc_ev_t;

typedef struct {
    /* Publish one mailbox byte into the console's program window. */
    void (*poke)(unsigned prog_addr, uint8_t value);

    bool (*link_up)(void);

    fb_status_t (*transact)(uint8_t device, uint8_t command,
                            const fb_param_t *params, unsigned nparams,
                            const uint8_t *payload, uint16_t payload_len,
                            uint32_t timeout_ms, fb_reply_t *reply);

    /* Send a frame with no reply expected -- used to ACK push frames. */
    void (*send_bare)(uint8_t device, uint8_t command,
                      const uint8_t *payload, uint16_t payload_len);

    /* A push stream has finished. `stream` is 0 for the ROM image and 1 for the
     * .cfg sibling. The cartridge lays a committed ROM into its spare bank
     * array and arms the swap; the emulator stages it and may dump it. */
    void (*stream_end)(int stream, const uint8_t *data, unsigned len,
                       bool aborted);

    /* Optional. */
    void (*wait_link_ms)(uint32_t ms);
    void (*bootsel)(void);
    void (*on_txn)(const fujimail_txn_t *txn);
    void (*on_dbc)(fujimail_dbc_ev_t ev, int stream, uint32_t expect,
                   unsigned got, bool aborted);
} fujimail_port_t;

void fujimail_init(const fujimail_port_t *port);

/* Paint the magic bytes and clear the rest of the page. */
void fujimail_paint(void);

/* One console write to the mailbox register file ($E0-$E3). */
void fujimail_write(uint8_t addr, uint8_t data);

/* Offer an inbound frame; true if it was a push frame and was consumed. */
bool fujimail_inbound(const fb_reply_t *frame);

#endif /* FUJIMAIL_H */
