/* fujitcp.h -- FujiBus over TCP to fujinet-pc's BoIP listener, for emulator models.
 * Unsolicited DBC push frames go to fujimail_inbound mid-transaction.
 *
 * The round trip is synchronous, so an emulated console calling it from the read sees
 * ACKSEQ updated on its first poll; on hardware (or a worker thread, as the 5200 device
 * runs it) it waits. A client must poll either way.
 */

#ifndef FUJITCP_H
#define FUJITCP_H

#include <stdbool.h>
#include <stdint.h>

#include "fujibus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called every few ms while a transaction waits, as fujibus_usb.h's idle hook is. */
void fujitcp_set_idle(void (*hook)(void));

/* Connect to "host:port" (NULL: $FUJINET_TCP, else 127.0.0.1:9995).
 * Returns 0 on success. */
int fujitcp_init(const char *hostport);
void fujitcp_close(void);
void fujitcp_abort(void);
bool fujitcp_active(void);

/* fujimail_port_t-shaped entry points. */
fb_status_t fujitcp_transact(uint8_t device, uint8_t command,
                             const fb_param_t *params, unsigned nparams,
                             const uint8_t *payload, uint16_t payload_len,
                             uint32_t timeout_ms, fb_reply_t *reply);
void fujitcp_send_bare(uint8_t device, uint8_t command,
                       const uint8_t *payload, uint16_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* FUJITCP_H */
