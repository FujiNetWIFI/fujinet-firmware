#ifndef CARTFS_BRIDGE_H
#define CARTFS_BRIDGE_H

// Services FUJI_DEVICEID_CARTFS requests from the ESP32-S3 (see fujibus.h)
// against our own local SD card via vfs.c, so a "SD" FujiNet host slot works
// on boards (pirto_ii_duo) where the SD is wired to us, not the ESP32-S3.
// When CONFIG_SD_STORAGE is off (fujicard) it still claims CARTFS frames,
// but answers every one with a NAK.
//
// Registered the same way dbc_inbound_handler() is -- see fujinet.c, which
// calls this first for FUJI_DEVICEID_CARTFS frames before falling through
// to its own DBC handling.

#include <stdbool.h>
#include "fujibus.h"

bool cartfs_inbound_handler(const fb_reply_t *req);

#endif /* CARTFS_BRIDGE_H */
