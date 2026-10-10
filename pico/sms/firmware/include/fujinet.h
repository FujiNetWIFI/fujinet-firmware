/* fujinet.h -- the cartridge's port into the shared mailbox service. */

#ifndef FUJINET_H
#define FUJINET_H

void fuji_service_init(void);

/* core0: drain the hotspot ring into the protocol. */
void fuji_mailbox_service(void);

#endif /* FUJINET_H */
