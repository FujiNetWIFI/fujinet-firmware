#ifndef _TNFSLIB_STREAM_LINK_H
#define _TNFSLIB_STREAM_LINK_H

// TNFS requests and replies over any tnfsStreamTransport. The link applies the framing and
// recovery rules; the transport, reply buffer and reply format belong to the mount.

#include <cstddef>
#include <cstdint>

#include "tnfslib.h"
#include "tnfslib_framing.h"
#include "tnfslib_stream_transport.h"

enum class tnfsStreamStatus
{
    REPLY,   // the answer, `length` bytes, is in `out`
    PENDING, // nothing complete yet; poll again until the transaction times out
    LOST,    // the connection was dropped; the next send reconnects
};

struct tnfsStreamReply
{
    tnfsStreamStatus status;
    int length; // reply length when REPLY, otherwise 0
};

class tnfsStreamLink
{
public:
    tnfsStreamLink(tnfsStreamTransport &transport, tnfsMountInfo &mount);

    // Connect first if needed, with a new empty reply buffer, then write the request's first
    // `len` bytes (header and payload).
    success_is_true send(const tnfsPacket &request, size_t len);

    // The answer to `request`. Reads at most the reply buffer's free space; the rest stays
    // queued in the transport. `out` is also the read buffer.
    tnfsStreamReply recv(uint8_t (&out)[TNFS_MAX_REPLY_SIZE], const tnfsPacket &request);

    // The caller stopped waiting for the answer to `request`. If what is buffered could be
    // completed into a wrong answer by the retry's reply, it is dropped with the connection.
    void expire(const tnfsPacket &request);

    // Close the connection and free the reply buffer.
    void drop();

private:
    tnfsReplyContext context(const tnfsPacket &request) const;
    bool infers() const;
    void settle();

    tnfsStreamTransport &_transport;
    tnfsMountInfo &_mount;
};

#endif // _TNFSLIB_STREAM_LINK_H
