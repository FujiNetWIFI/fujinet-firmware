/* fujimail.h -- the mailbox service: register decode, the SEQ/ACKSEQ interlock
 * and the DBC ROM-push receiver. Shared by the cartridge and the o2em model
 * through a port interface.
 */

#ifndef FUJIMAIL_H
#define FUJIMAIL_H

#include <stdbool.h>
#include <stdint.h>

#include "fujibus.h"
#include "fuji_mailbox.h"
#include "o2map.h"

/* A full APPBANK image, the largest o2map accepts (more than 8 legacy 3K banks). */
#define FUJIMAIL_STAGE_MAX (O2_APP_FIXED_BYTES + O2_APP_MAXPAGES * O2_APP_PAGE_BYTES)
#define FUJIMAIL_RX_MAX    1024

/* An APPBANK cart serves the read window from here; fujimail_rxn is stored
 * after the bytes are copied. */
extern uint8_t fujimail_rx[FUJIMAIL_RX_MAX];
extern volatile unsigned fujimail_rxn;

/* Set by the port while an APPBANK image is served from the stage buffer;
 * a further push is refused with FN_BOOT_ERR_BUSY. */
extern volatile bool fujimail_stage_live;

/* For logging only. */
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
    void (*poke)(unsigned prog_addr, uint8_t value);

    bool (*link_up)(void);

    fb_status_t (*transact)(uint8_t device, uint8_t command,
                            const fb_param_t *params, unsigned nparams,
                            const uint8_t *payload, uint16_t payload_len,
                            uint32_t timeout_ms, fb_reply_t *reply);

    /* No reply expected; ACKs push frames. */
    void (*send_bare)(uint8_t device, uint8_t command,
                      const uint8_t *payload, uint16_t payload_len);

    /* `stream` is 0 for the ROM image, 1 for the .cfg sibling. */
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
