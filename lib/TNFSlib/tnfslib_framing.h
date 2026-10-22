#ifndef _TNFSLIB_FRAMING_H
#define _TNFSLIB_FRAMING_H

// TNFS-over-TCP reply framing. TNFS has no length field, so a reply ends where its command,
// status and payload say; optional fields are framed as tnfsReplyFormat says.

#include <cstdint>

#include "tnfslib.h"

enum class tnfsFrameStatus
{
    NEED_MORE, // the reply at the front is incomplete; nothing is consumed
    COMPLETE,  // a whole reply of `length` bytes is at the front
    MALFORMED, // the front cannot be a reply; with no sync marker, reconnect
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
    EARLIER,     // an earlier request's reply: late, duplicated, or received before the send
    SESSIONLESS, // TNFSD's session-0 error for request bytes it could not tie to a session
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
    FAILED,  // neither: the buffer is discarded and the connection must be replaced
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

    // COMPLETE moves the reply to `out`; MALFORMED discards the buffer. TNFSD's shapes only,
    // with no request to check against.
    tnfsFrame take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE]);

    // As above, checked against `context`; `kind` says what the reply is. A previous optional
    // field is skipped only when a reply fits better after it; a tie is MALFORMED.
    tnfsFrame take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE], const tnfsReplyContext &context,
                   tnfsReplyKind &kind);

    // Whether a retry's reply could complete a cut-off answer; if so, reconnect first.
    bool may_hold_cut_answer(const tnfsReplyContext &context) const;

    // The optional field of UNKNOWN use that may follow the last reply taken, or NONE.
    tnfsOptionalField unsettled() const { return _optional; }

    // Frame the answer to `fence`, a session-0 request sent right after the unsettled reply.
    // It starts 00 00 <sequence> <command> and a field ends in a zero byte, so it fits at
    // the front (ABSENT) or after the field (PRESENT), never both.
    tnfsFenceVerdict settle(const tnfsReplyContext &fence);

    // Whether a retry's duplicate of an answered request may still arrive.
    bool replies_pending() const { return _outstanding > 0; }

    // Call just before `sequence` first goes out, with the bytes received but not appended:
    // none of them can start its answer. Retries keep the mark.
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
