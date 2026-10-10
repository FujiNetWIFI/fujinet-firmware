#ifndef HOTSYNC_LINK_H
#define HOTSYNC_LINK_H

#include <cstddef>
#include <cstdint>

// Raw byte pipe to a Palm device: a UART, a TCP socket, or a test fixture.
class HotSyncLink
{
public:
    virtual ~HotSyncLink() = default;

    // Reads up to len bytes, waiting at most timeout_ms for the first one.
    // Returns the count read, 0 on timeout, or -1 once the link is closed.
    virtual int read(uint8_t *buf, size_t len, uint32_t timeout_ms) = 0;
    virtual int write(const uint8_t *buf, size_t len) = 0;

    // Only a UART honours this; sockets and fixtures ignore it.
    virtual void set_baud_rate(uint32_t baud) { (void)baud; }
};

#endif // HOTSYNC_LINK_H
