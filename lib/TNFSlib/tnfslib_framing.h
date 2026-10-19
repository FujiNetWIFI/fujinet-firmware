#ifndef _TNFSLIB_FRAMING_H
#define _TNFSLIB_FRAMING_H

// TNFS reply framing for byte-stream transports (TNFS over TCP). TNFS has no length field,
// so where a reply ends is derived from its command, its status and its self-describing
// payload fields, as laid out in tnfs-protocol.md. Read boundaries are never trusted.
//
// tnfs_frame_reply() takes a successful STAT to be the fixed 22-byte block that TNFSD sends.
// Some servers (paumaed) also send fields the protocol leaves optional: the uid and gid
// strings after a STAT, and a zero byte count after a READ or WRITE that failed. Whether a
// server sends them is its reply format (tnfsReplyFormat). Where the format says, a field is
// framed as part of its reply. Where it is UNKNOWN, the bytes alone cannot always tell: a
// field delayed past the next request can read as that request's answer. The transport
// therefore settles it before sending anything else (settle()), or reconnects; only its
// INFER policy, for tests only, leaves it to the next reply's header.

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

// The request whose reply is awaited. Only one is outstanding per connection.
struct tnfsReplyContext
{
    uint16_t session; // the mount's session id; a MOUNT reply carries a new one
    uint8_t sequence;
    uint8_t command;
    tnfsReplyFormat format{}; // the fields the server is known to send or leave out
};

// What a reply taken in a request context is.
enum class tnfsReplyKind
{
    ANSWER,      // sequence and command of the request, and its session (or 0 with INVALID)
    EARLIER,     // this session's reply to an earlier request: another sequence (late, or a
                 // retry's duplicate), or received before the pending request went out
    SESSIONLESS, // status-only error with session 0 for no request of ours: TNFSD's answer
                 // to request bytes it could not tie to a session (a split request)
};

// What the last reply taken in a request context may still have after it.
enum class tnfsOptionalField
{
    NONE,
    STAT_STRINGS, // uid and gid strings
    READ_COUNT,   // 00 00 after a failed READ
    WRITE_COUNT,  // 00 00 after a failed WRITE
};

// What the answer to a fence request (see settle()) shows about an unsettled field.
enum class tnfsFenceVerdict
{
    NEED_MORE,
    ABSENT,  // the fence's answer came right after the reply: the server left the field out
    PRESENT, // the field, then the fence's answer: the server sent it
    FAILED,  // neither: all buffered bytes are discarded and the connection must be replaced
};

// Fixed-capacity reassembly of a reply byte stream: append at most space() bytes of what
// the transport delivered, then take one complete reply at a time.
class tnfsReplyAssembler
{
public:
    // An unsettled optional field plus one maximum reply: when full, take() never waits.
    static constexpr int CAPACITY = 2 * TNFS_MAX_REPLY_SIZE;

    // Copy up to `len` bytes into free space; returns how many were accepted.
    int append(const uint8_t *data, int len);

    // COMPLETE: the reply is copied to `out` and removed; anything after it is kept.
    // NEED_MORE: nothing changes. MALFORMED: all buffered bytes are discarded.
    // Frames TNFSD's shapes only, with no request to check against.
    tnfsFrame take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE]);

    // As above, for the pending request `context`; `kind` says what the reply is. Bytes
    // that may be the previous reply's optional field are skipped only when a reply fits
    // after them better than at their start; when both fit alike, the stream is MALFORMED.
    tnfsFrame take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE], const tnfsReplyContext &context,
                   tnfsReplyKind &kind);

    // Whether a retry's reply could complete a cut-off answer to `context` into a wrong
    // one: the connection must then be replaced before retrying.
    bool may_hold_cut_answer(const tnfsReplyContext &context) const;

    // The optional field that may follow the last reply taken, when context.format left its
    // use UNKNOWN; NONE otherwise.
    tnfsOptionalField unsettled() const { return _optional; }

    // Frame the answer to `fence`, a request sent with session 0 right after the reply that
    // left unsettled(), and with no other reply outstanding. Servers answer a request for no
    // session with a status-only reply carrying session 0 and the request's sequence and
    // command (TNFSD and paumaed: INVALID). Such an answer begins with two zero bytes and
    // then the fence's command, which is not zero; the field ends in a zero byte. So the
    // bytes match the answer either at the front (ABSENT) or right after the field
    // (PRESENT), never both, and the first byte that rules one out settles it. No waiting
    // beyond the fence's own answer, whatever the field holds.
    tnfsFenceVerdict settle(const tnfsReplyContext &fence);

    // Whether a reply from this session may still come for a request already answered:
    // a retry's duplicate. Counts copies sent against replies of ours taken.
    bool replies_pending() const { return _outstanding > 0; }

    // Call just before request `sequence` first goes out, with the bytes the connection has
    // received but not yet appended. A server cannot answer a request it has not received,
    // so none of the bytes received so far can begin its answer. Retries keep the mark.
    void request_sent(uint8_t sequence, int unread);

    // Drop everything, e.g. when the connection is replaced.
    void reset()
    {
        _len = 0;
        _optional = tnfsOptionalField::NONE;
        _stream_pos = 0;
        _sent_marked = false;
        _outstanding = 0;
    }

    int buffered() const { return _len; }
    int space() const { return CAPACITY - _len; }

private:
    // Whether a reply starting `offset` bytes into the buffer may answer `context`.
    bool after_request(int offset, const tnfsReplyContext &context) const
    {
        return !_sent_marked || _sent_sequence != context.sequence ||
               _stream_pos + static_cast<uint64_t>(offset) >= _answer_from;
    }

    uint8_t _buf[CAPACITY];
    int _len = 0;
    tnfsOptionalField _optional = tnfsOptionalField::NONE;
    uint64_t _stream_pos = 0;  // stream offset of _buf[0]
    uint64_t _answer_from = 0; // stream offset where the answer to _sent_sequence may begin
    uint8_t _sent_sequence = 0;
    bool _sent_marked = false;
    int _outstanding = 0; // requests sent minus replies of ours (ANSWER, EARLIER) taken
};

#endif // _TNFSLIB_FRAMING_H
