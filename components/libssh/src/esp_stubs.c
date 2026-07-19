/*
 * esp_stubs.c - Stub implementations for functions not available on ESP-IDF
 *
 * This file provides minimal stub implementations for functions that are
 * not available on the ESP-IDF platform but are referenced by libssh code.
 */

#include "../config.h"

#ifdef ESP_PLATFORM

#include <stddef.h>
#include <string.h>
#include <esp_idf_version.h>

/* Stub for encode_current_tty_opts - TTY options not supported on ESP-IDF */
int encode_current_tty_opts(unsigned char *buf, size_t buflen)
{
    /* Return empty TTY options (just the terminator) */
    if (buf != NULL && buflen > 0) {
        buf[0] = 0; /* TTY_OP_END */
    }
    return 1; /* Length of 1 byte (just the terminator) */
}

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 5, 0)
/* Stub for if_nametoindex - Network interface indexing not needed on ESP-IDF */
unsigned int if_nametoindex(const char *ifname)
{
    /* Return 0 to indicate interface not found */
    (void)ifname;
    return 0;
}
#endif

#endif /* ESP_PLATFORM */
