/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef BASE64_H
#define BASE64_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif 

    char * base64_encode(const void *src, size_t len, size_t *out_len);
    unsigned char * base64_decode(const char *src, size_t len, size_t *out_len);
    char * base64_url_encode(const void *src, size_t len, size_t *out_len);
    unsigned char * base64_url_decode(const char *src, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* BASE64_H */