#ifndef _TNFSLIB_STREAM_TRANSPORT_H
#define _TNFSLIB_STREAM_TRANSPORT_H

// The byte-stream connection a TNFS mount talks through. tnfsStreamLink applies the
// framing and recovery rules on top of it; an implementation only moves bytes.

#include <cstddef>
#include <cstdint>

#include "compat_inet.h"
#include "global_types.h"

class tnfsStreamTransport
{
public:
    virtual ~tnfsStreamTransport() = default;

    // Connect to `address`, or to `hostname` while `address` is IPADDR_NONE; a connect by
    // name may store the address it reached in `address`.
    virtual success_is_true open(const char *hostname, in_addr_t &address, uint16_t port,
                                 int timeout_ms) = 0;
    // False once the connection is closed by either side or has failed.
    virtual bool is_open() = 0;
    // Bytes that can be read now without waiting.
    virtual size_t available() = 0;
    virtual int read(uint8_t *buf, size_t size) = 0;
    // Bytes written; fewer than `size` means the request did not go out whole.
    virtual size_t write(const uint8_t *buf, size_t size) = 0;
    // Close the connection; whatever was received and not read is lost.
    virtual void close() = 0;
};

#endif // _TNFSLIB_STREAM_TRANSPORT_H
