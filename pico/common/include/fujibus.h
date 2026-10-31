// FujiBus/SLIP client for the cartridge <-> ESP32-S3 (fujiversal-rs232) link: builds and
// parses single request/reply packets, bit-compatible with the ESP32's FujiBus.
#ifndef FUJIBUS_H
#define FUJIBUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FUJI_DEVICEID_FUJINET 0x70

// The network devices N1..N8 (FUJI_DEVICEID_NETWORK..NETWORK_LAST in the main
// tree's include/fujiDeviceID.h).
#define FUJI_DEVICEID_NET_FIRST 0x71
#define FUJI_DEVICEID_NET_LAST  0x78

// The first disk and printer, and the serial device a modem answers as.
#define FUJI_DEVICEID_DISK    0x31
#define FUJI_DEVICEID_PRINTER 0x40
#define FUJI_DEVICEID_SERIAL  0x50

// The device the ESP32-S3 addresses when it pushes a ROM (and optional .cfg sibling) to the
// cartridge mid-MOUNT_IMAGE. Matches the main tree's include/fujiDeviceID.h.
#define FUJI_DEVICEID_DBC 0xFF

// The main tree's CMD (include/fujiCommandID.h) in plain C. The ROM push uses
// the NET_* commands.
#define CMD_NET_OPEN   0x4F
#define CMD_NET_READ   0x52
#define CMD_NET_STATUS 0x53
#define CMD_NET_WRITE  0x57
#define CMD_NET_CLOSE  0x43

#define CMD_FUJI_GET_ADAPTERCONFIG_EXTENDED 0xC4
#define CMD_FUJI_MOUNT_IMAGE 0xF8
#define CMD_FUJI_COPY_FILE 0xD8
#define CMD_FUJI_SCAN_NETWORKS 0xFD
#define CMD_FUJI_SET_DEVICE_FULLPATH 0xE2
#define CMD_FUJI_UNMOUNT_IMAGE 0xE9
#define CMD_FUJI_MOUNT_ALL 0xD7
#define CMD_FUJI_NEW_DISK 0xE7
#define CMD_FUJI_WRITE_DEVICE_SLOTS 0xF1
#define CMD_FUJI_UNMOUNT_HOST 0xE6
#define CMD_FUJI_ACK 0x06
#define CMD_FUJI_NAK 0x15

// The ESP32's packed AdapterConfigExtended reply.
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
    uint8_t command;   // CMD::FUJI_ACK or CMD::FUJI_NAK
    const uint8_t *data;
    uint16_t data_len;
} fb_reply_t;

// params and payload may be NULL/0. Returns the frame length, or 0 if it would not fit.
size_t fujibus_build_request(uint8_t device, uint8_t command,
                              const fb_param_t *params, unsigned nparams,
                              const uint8_t *payload, uint16_t payload_len,
                              uint8_t *out, size_t out_cap);

// Exactly one frame; bytes before the first 0xC0 are skipped, as on the ESP32.
// Decodes in place: `in` is modified and reply->data points into it.
bool fujibus_parse_reply(const uint8_t *in, size_t in_len, fb_reply_t *reply);

// Known-good request/reply vectors, in memory only.
bool fujibus_selftest(void);

#endif /* FUJIBUS_H */
