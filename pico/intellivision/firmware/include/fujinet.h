#ifndef FUJINET_H
#define FUJINET_H

#include "fujibus.h"

// Services at most one FujiNet mailbox transaction per call (see
// fuji_mailbox.h for the SEQ/ACKSEQ interlock) and, independent of that,
// checks the BOOTSEL doorbell. Call from the core0 loop alongside tud_task().
void fuji_mailbox_service(void);

// Registered with fujibus_set_inbound_handler() in Inty_cart_main(): handles
// the ESP32-S3 pushing a ROM (and optional .cfg sibling) to
// FUJI_DEVICEID_DBC mid-MOUNT_IMAGE-transaction. See fujinet.c for the full
// protocol writeup.
bool dbc_inbound_handler(const fb_reply_t *req);

#endif /* FUJINET_H */
