/* fujinet.h -- the cartridge's port into the shared mailbox service. */

#ifndef FUJINET_H
#define FUJINET_H

void fuji_service_init(void);

/* core0: drain the hotspot ring into the protocol, then do the work core1
 * deferred (see fuji_cart.h). */
void fuji_mailbox_service(void);

/* core0, from fuji_cart.c. */
void fuji_cart_service_deferred(void);

#endif /* FUJINET_H */
