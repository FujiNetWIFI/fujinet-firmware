/* fujinet.h -- the cartridge's mailbox service. */
#ifndef FUJINET_H
#define FUJINET_H

#include <stdbool.h>
#include "fujibus.h"

void fuji_service_init(void);

/* Drain the write ring and run any transaction the console has launched.
 * Called from the cartridge's main loop on core0; returns quickly when idle. */
void fuji_mailbox_service(void);

/* Inbound DBC push frames, registered with fujibus_set_inbound_handler(). */
bool dbc_inbound_handler(const fb_reply_t *req);

/* Stage a committed image and arm core1's swap. */
void fuji_boot_stage(void);

#endif /* FUJINET_H */
