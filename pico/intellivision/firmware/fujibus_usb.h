// TinyUSB CDC transport for fujibus.c's protocol codec. Split out from
// fujibus.c/.h deliberately: fujibus.c has no hardware or USB dependency
// and builds/tests on a desktop; this file is RP2040/TinyUSB-only glue on
// top of it.
#ifndef FUJIBUS_USB_H
#define FUJIBUS_USB_H

#include "fujibus.h"

// Registers a handler for INBOUND frames that arrive while fujibus_transact()
// is waiting on a reply -- specifically, the ESP32-S3 pushing a ROM (and an
// optional .cfg sibling) to FUJI_DEVICEID_DBC mid-MOUNT_IMAGE-transaction
// (see fuji_network_boot() in inty_cart.c). fujibus.c/fujibus_usb.c stay
// protocol-agnostic: the handler decides what counts as "inbound" (by
// device id or whatever else) and does the actual work; this file only
// dispatches to it.
//
// Called once per complete SLIP frame received during a wait. Return true
// if the frame was an inbound request and has been fully handled
// (including sending any reply of its own over the same link) -- the wait
// for the real reply continues, with a fresh timeout. Return false to
// treat the frame as the reply fujibus_transact() itself is waiting for.
typedef bool (*fujibus_inbound_fn)(const fb_reply_t *frame);
void fujibus_set_inbound_handler(fujibus_inbound_fn handler);

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
