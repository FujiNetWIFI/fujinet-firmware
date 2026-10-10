/* fujitcp.h -- FujiBus over a TCP socket, for emulator models.
 *
 * The transport half of the o2em model's fujinet.c, split out so the MAME
 * cart device can reuse it unchanged: SLIP-framed FujiBus request/reply to
 * fujinet-pc's BoIP listener (default 127.0.0.1:9995), with unsolicited DBC
 * push frames handed to fujimail_inbound mid-transaction.
 *
 * Fidelity note: the socket round trip is synchronous, so an emulated
 * console sees ACKSEQ already updated on its first poll. Real hardware makes
 * it wait. The client code is identical either way -- it must poll
 * regardless -- but an emulator using this will not catch a client that
 * forgets to (test_fujimail.c and the SLICE_ECHO handshake exist for that).
 */

#ifndef FUJITCP_H
#define FUJITCP_H

#include <stdbool.h>
#include <stdint.h>

#include "fujibus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connect to "host:port" (NULL: $FUJINET_TCP, else 127.0.0.1:9995).
 * Returns 0 on success. */
int fujitcp_init(const char *hostport);
void fujitcp_close(void);
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
