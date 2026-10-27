#ifndef FUJINET_H
#define FUJINET_H

#include "fujibus.h"

// At most one mailbox transaction per call, plus the BOOTSEL doorbell.
// Call from the core0 loop alongside tud_task().
void fuji_mailbox_service(void);

// sleep_ms() that keeps USB alive.
void fuji_wait_ms_pumped(uint32_t ms);

// Inbound handler for the ESP32-S3's ROM push to FUJI_DEVICEID_DBC.
bool dbc_inbound_handler(const fb_reply_t *req);

#endif /* FUJINET_H */
