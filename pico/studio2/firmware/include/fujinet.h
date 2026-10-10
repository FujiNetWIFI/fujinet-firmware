/* fujinet.h -- the cartridge's port into the shared mailbox service. */

#ifndef FUJINET_H
#define FUJINET_H

#include <stdint.h>

/* `config` is what FN_REG_CONFIG stages: the baked CONFIG. */
void fuji_service_init(const uint8_t *config, uint32_t config_len);

/* core0: drain the ring into the protocol and the text engine. */
void fuji_mailbox_service(void);

#endif /* FUJINET_H */
