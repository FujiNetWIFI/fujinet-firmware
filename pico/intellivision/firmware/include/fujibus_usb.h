// TinyUSB CDC transport for the fujibus.c codec, kept apart so the codec
// builds on a host.
#ifndef FUJIBUS_USB_H
#define FUJIBUS_USB_H

#include "fujibus.h"

// Called for each frame received while fujibus_transact() waits, such as
// the ESP32-S3's mid-MOUNT_IMAGE ROM push. Return true if the handler
// consumed it (sending any reply itself); the wait continues with a fresh
// timeout. Return false to make it the transaction's reply.
typedef bool (*fujibus_inbound_fn)(const fb_reply_t *frame);
void fujibus_set_inbound_handler(fujibus_inbound_fn handler);

// Sends one frame with no reply expected, e.g. from an inbound handler.
void fujibus_send_frame(const uint8_t *frame, size_t len);

// rx_buf is the raw read buffer and backs reply->data; keep it alive while
// reply is in use.
fb_status_t fujibus_transact(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *rx_buf, size_t rx_cap,
                              fb_reply_t *reply, uint32_t timeout_ms);

#endif /* FUJIBUS_USB_H */
