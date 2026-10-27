/* fujinet.h -- the cartridge's side of the mailbox. */
#ifndef FUJINET_H
#define FUJINET_H

void fuji_service_init(void);

/* core0 main loop: drain core1's write ring and run any launched transaction. */
void fuji_mailbox_service(void);

#endif /* FUJINET_H */
