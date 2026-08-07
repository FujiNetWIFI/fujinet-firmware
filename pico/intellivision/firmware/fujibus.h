// FujiBus/SLIP client for the RP2040 <-> ESP32-S3 (fujiversal-rs232) link.
//
// Bit-compatible plain-C port of the wire format implemented in
// lib/bus/rs232/FujiBusPacket.cpp in the main fujinet-firmware tree. See
// that file for the authoritative protocol definition; this is a client,
// not a reimplementation of the bus service, and only needs to build and
// parse single request/reply packets.
#ifndef FUJIBUS_H
#define FUJIBUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FUJI_DEVICEID_FUJINET 0x70

#define FUJICMD_GET_ADAPTERCONFIG_EXTENDED 0xC4
#define FUJICMD_ACK 0x06
#define FUJICMD_NAK 0x15

// AdapterConfigExtended, packed layout matching lib/device/fujiDevice.h.
#define FUJI_ADAPTERCONFIG_EXTENDED_SIZE 240

typedef enum {
    FB_OK = 0,
    FB_ENOLINK,   // no USB CDC connection to the ESP32-S3
    FB_ETIMEOUT,  // no reply (or an incomplete one) within the deadline
    FB_EBADFRAME, // SLIP/length/checksum validation failed
    FB_ETOOBIG,   // request or reply would not fit in the static buffers
} fb_status_t;

// One parameter to encode into a request. `size` is 1, 2, or 4 bytes;
// `value` is truncated to that width and encoded little-endian.
typedef struct {
    uint32_t value;
    uint8_t size;
} fb_param_t;

typedef struct {
    uint8_t device;
    uint8_t command;   // FUJICMD_ACK or FUJICMD_NAK
    const uint8_t *data;
    uint16_t data_len;
} fb_reply_t;

// Builds a SLIP-framed FujiBus request for `device`/`command` with the given
// params (may be NULL/0) and payload (may be NULL/0), into `out` (capacity
// `out_cap`). Returns the frame length, or 0 if it would not fit.
size_t fujibus_build_request(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *out, size_t out_cap);

// Parses one SLIP-framed FujiBus packet out of `in` (exactly one frame,
// leading garbage before the first 0xC0 is skipped same as the ESP32 side).
// On success, `reply->data` points into `in` (no copy) and remains valid
// only as long as `in` does. Returns false on any framing/length/checksum
// error.
bool fujibus_parse_reply(const uint8_t *in, size_t in_len, fb_reply_t *reply);

// Runs the codec's self-test (known-good request/reply vectors) purely in
// memory, no USB or hardware involved. Returns true if everything matches.
bool fujibus_selftest(void);

#endif /* FUJIBUS_H */
