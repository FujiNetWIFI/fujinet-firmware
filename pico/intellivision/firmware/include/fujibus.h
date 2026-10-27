// FujiBus/SLIP client for the RP2040 <-> ESP32-S3 (fujiversal-rs232) link:
// builds and parses single packets, bit-compatible with the ESP32 side.
#ifndef FUJIBUS_H
#define FUJIBUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FUJI_DEVICEID_FUJINET 0x70

// Device the ESP32-S3 pushes a ROM (and optional .cfg) to, mid-MOUNT_IMAGE.
// Matches FUJI_DEVICEID_DBC in the main tree's include/fujiDeviceID.h.
#define FUJI_DEVICEID_DBC 0xFF

// The main tree's CMD::NET_* (include/fujiCommandID.h), used by the ROM push.
#define NETCMD_OPEN  0x4F
#define NETCMD_WRITE 0x57
#define NETCMD_CLOSE 0x43

#define FUJICMD_GET_ADAPTERCONFIG_EXTENDED 0xC4
#define FUJICMD_MOUNT_IMAGE 0xF8
#define FUJICMD_SET_DEVICE_FULLPATH 0xE2
#define FUJICMD_ACK 0x06
#define FUJICMD_NAK 0x15

// Size of the ESP32's packed AdapterConfigExtended.
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

#define FB_MAX_PARAMS 8

typedef struct {
    uint8_t device;
    uint8_t command;   // FUJICMD_ACK or FUJICMD_NAK in a reply
    const uint8_t *data;
    uint16_t data_len;
    uint8_t nparams;   // params past FB_MAX_PARAMS are skipped
    uint32_t params[FB_MAX_PARAMS];
} fb_reply_t;

// params and payload may be NULL/0. Returns the frame length, or 0 if it
// would not fit.
size_t fujibus_build_request(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *out, size_t out_cap);

// Parses exactly one frame; bytes before the first 0xC0 are skipped.
// Decodes in place: `in` is overwritten and reply->data points into it.
bool fujibus_parse_reply(const uint8_t *in, size_t in_len, fb_reply_t *reply);

// In-memory check against known-good request/reply vectors.
bool fujibus_selftest(void);

#endif /* FUJIBUS_H */
