/* fujibus_usb.h -- FujiBus over the RP2040's USB CDC link to the ESP32-S3. */
#ifndef FUJIBUS_USB_H
#define FUJIBUS_USB_H

#include <stdbool.h>
#include <stdint.h>
#include "fujibus.h"

/* A frame the ESP32-S3 sends unsolicited, mid-transaction. Return true if it
 * was consumed, in which case the wait continues with a refreshed deadline. */
typedef bool (*fujibus_inbound_fn)(const fb_reply_t *frame);
void fujibus_set_inbound_handler(fujibus_inbound_fn handler);

bool fujibus_link_up(void);

/* Pump USB for `ms`, never back-to-back. */
void fuji_wait_ms_pumped(uint32_t ms);

/* One request/response round trip. `timeout_ms` is per frame, so consuming an
 * inbound push frame restarts it. */
fb_status_t fujibus_transact(uint8_t device, uint8_t command,
                             const fb_param_t *params, unsigned nparams,
                             const uint8_t *payload, uint16_t payload_len,
                             uint32_t timeout_ms, fb_reply_t *reply);

/* Send a bare frame with no reply expected (used to ACK push frames). */
void fujibus_send_bare(uint8_t device, uint8_t command,
                       const uint8_t *payload, uint16_t payload_len);

#endif /* FUJIBUS_USB_H */
