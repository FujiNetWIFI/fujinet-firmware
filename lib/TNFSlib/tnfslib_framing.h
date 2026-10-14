#ifndef _TNFSLIB_FRAMING_H
#define _TNFSLIB_FRAMING_H

// TNFS reply framing for byte-stream transports (TNFS over TCP). TNFS has no length field,
// so where a reply ends is derived from its command, its status and its self-describing
// payload fields, as laid out in tnfs-protocol.md. Read boundaries are never trusted.
//
// A successful STAT reply is taken to be the fixed 22-byte block that TNFSD sends. The
// protocol also allows optional uid and gid strings after it. From a server that sends
// them, those bytes are parsed as the next reply, which then usually frames as MALFORMED.

#include <cstdint>

#include "tnfslib.h"

enum class tnfsFrameStatus
{
    NEED_MORE, // the reply at the front is incomplete; nothing is consumed
    COMPLETE,  // a whole reply of `length` bytes is at the front
    MALFORMED, // the front cannot be a valid reply. With no sync marker in the stream,
               // the only recovery is to drop the connection and reconnect
};

struct tnfsFrame
{
    tnfsFrameStatus status;
    int length; // reply length in bytes when COMPLETE, otherwise 0
};

// Largest reply a server may send: header plus the biggest payload.
constexpr int TNFS_MAX_REPLY_SIZE = TNFS_HEADER_SIZE + TNFS_PAYLOAD_SIZE;

// Classify the reply at the front of the first `have` bytes of `buf`. Pure; never reads
// beyond `have` bytes and never reports a length above TNFS_MAX_REPLY_SIZE.
tnfsFrame tnfs_frame_reply(const uint8_t *buf, int have);

// Fixed-capacity reassembly of a reply byte stream: append at most space() bytes of what
// the transport delivered, then take one complete reply at a time. One maximum reply is
// enough: a full assembler always frames as COMPLETE or MALFORMED, never NEED_MORE.
class tnfsReplyAssembler
{
public:
    static constexpr int CAPACITY = TNFS_MAX_REPLY_SIZE;

    // Copy up to `len` bytes into free space; returns how many were accepted.
    int append(const uint8_t *data, int len);

    // COMPLETE: the reply is copied to `out` and removed; anything after it is kept.
    // NEED_MORE: nothing changes. MALFORMED: all buffered bytes are discarded.
    tnfsFrame take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE]);

    // Drop everything, e.g. when the connection is replaced.
    void reset() { _len = 0; }

    int buffered() const { return _len; }
    int space() const { return CAPACITY - _len; }

private:
    uint8_t _buf[CAPACITY];
    int _len = 0;
};

#endif // _TNFSLIB_FRAMING_H
