/* fujinet.h -- the cartridge's side of the mailbox. */
#ifndef FUJINET_H
#define FUJINET_H

void fuji_service_init(void);

/* Drain core1's write ring and run whatever the console has launched. Called
 * from the cartridge's main loop on core0; returns quickly when idle. */
void fuji_mailbox_service(void);

#endif /* FUJINET_H */
