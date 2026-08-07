// TinyUSB CDC transport for fujibus.c's protocol codec. Split out from
// fujibus.c/.h deliberately: fujibus.c has no hardware or USB dependency
// and builds/tests on a desktop; this file is RP2040/TinyUSB-only glue on
// top of it.
#ifndef FUJIBUS_USB_H
#define FUJIBUS_USB_H

#include "fujibus.h"

// Sends one request and waits up to `timeout_ms` for the SLIP-framed reply.
// `rx_buf` (capacity `rx_cap`) is used both as the raw USB read buffer and,
// via fujibus_parse_reply()'s in-place decode, as the storage backing
// `reply->data` on success -- it must stay valid as long as `reply` is used.
fb_status_t fujibus_transact(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *rx_buf, size_t rx_cap,
                              fb_reply_t *reply, uint32_t timeout_ms);

#endif /* FUJIBUS_USB_H */
