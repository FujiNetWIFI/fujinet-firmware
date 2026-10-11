/* fujinet.h -- the cartridge's port into the shared mailbox service. */

#ifndef FUJINET_H
#define FUJINET_H

void fuji_service_init(void);

/* core0: drain the ring into the protocol, the loader and the POKEY. */
void fuji_mailbox_service(void);

/* core0: restore and save the High Score Cart's RAM. */
void fuji_hsc_service(void);

#endif /* FUJINET_H */
