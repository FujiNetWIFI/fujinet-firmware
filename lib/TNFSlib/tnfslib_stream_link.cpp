#include "tnfslib_stream_link.h"

#include <algorithm>

#include "../../include/debug.h"

#include "fnSystem.h"

namespace
{
uint8_t field_bit(tnfsOptionalField field) { return static_cast<uint8_t>(1u << (static_cast<int>(field) - 1)); }

const char *field_name(tnfsOptionalField field)
{
    return field == tnfsOptionalField::STAT_STRINGS ? "STAT uid/gid strings"
           : field == tnfsOptionalField::READ_COUNT ? "failed READ count"
                                                    : "failed WRITE count";
}

// The format replies are framed with: an ABSENT that no fence has confirmed counts as UNKNOWN.
tnfsReplyFormat trusted_format(const tnfsMountInfo &m)
{
    tnfsReplyFormat f = m.tcp_reply_format;
    auto check = [&m](tnfsFieldUse &use, tnfsOptionalField field)
    {
        if (use == tnfsFieldUse::ABSENT && !(m.tcp_format_verified & field_bit(field)))
            use = tnfsFieldUse::UNKNOWN;
    };
    check(f.stat_strings, tnfsOptionalField::STAT_STRINGS);
    check(f.read_count, tnfsOptionalField::READ_COUNT);
    check(f.write_count, tnfsOptionalField::WRITE_COUNT);
    return f;
}

// Record what a fence showed; it overrides an expectation it contradicts.
void set_field_use(tnfsMountInfo &m, tnfsOptionalField field, tnfsFieldUse use)
{
    tnfsReplyFormat &format = m.tcp_reply_format;
    tnfsFieldUse &slot = field == tnfsOptionalField::STAT_STRINGS ? format.stat_strings
                         : field == tnfsOptionalField::READ_COUNT ? format.read_count
                                                                  : format.write_count;
    if (slot != tnfsFieldUse::UNKNOWN && slot != use)
        Debug_printf("TNFS server contradicts the expected reply format (%s)\r\n", field_name(field));
    slot = use;
    if (use == tnfsFieldUse::ABSENT)
        m.tcp_format_verified |= field_bit(field);
}
} // namespace

tnfsStreamLink::tnfsStreamLink(tnfsStreamTransport &transport, tnfsMountInfo &mount)
    : _transport(transport), _mount(mount)
{
}

success_is_true tnfsStreamLink::send(const tnfsPacket &request, size_t len)
{
    if (!_transport.is_open())
    {
        if (_transport.open(_mount.hostname, _mount.host_ip, _mount.port, TNFS_TIMEOUT).is_error())
        {
            Debug_println("Can't connect to the TNFS server");
            RETURN_ERROR_AS_FALSE();
        }
        if (_mount.tcp_replies)
            _mount.tcp_replies->reset();
        else
            _mount.tcp_replies = tnfs_new_reply_assembler();
        if (_mount.tcp_replies == nullptr)
        {
            Debug_println("No memory for TNFS stream replies");
            _transport.close();
            RETURN_ERROR_AS_FALSE();
        }
    }
    _mount.tcp_replies->request_sent(request.sequence_num, static_cast<int>(_transport.available()));
    RETURN_SUCCESS_IF(_transport.write(request.rawData, len) == len);
}

tnfsReplyContext tnfsStreamLink::context(const tnfsPacket &request) const
{
    uint16_t session = TNFS_UINT16_FROM_HILOBYTES(request.session_idh, request.session_idl);
    return {session, request.sequence_num, request.command, trusted_format(_mount)};
}

// Whether replies are left to the next reply's header (tests only; see tnfsFormatPolicy).
bool tnfsStreamLink::infers() const
{
#ifdef UNIT_TESTS
    return _mount.tcp_format_policy == tnfsFormatPolicy::INFER;
#else
    return false;
#endif
}

tnfsStreamReply tnfsStreamLink::recv(uint8_t (&out)[TNFS_MAX_REPLY_SIZE], const tnfsPacket &request)
{
    tnfsReplyAssembler *replies = _mount.tcp_replies.get();
    if (replies == nullptr)
        return {tnfsStreamStatus::PENDING, 0};

    size_t want = std::min({_transport.available(), static_cast<size_t>(replies->space()), sizeof(out)});
    if (want > 0)
        replies->append(out, _transport.read(out, want));

    const tnfsReplyContext ctx = context(request);
    tnfsReplyKind kind;
    tnfsFrame frame = replies->take(out, ctx, kind);
    // Only the answer goes up: an earlier request's reply may even carry this sequence, and
    // TNFSD's reply to request bytes it could not tie to a session answers nothing we sent.
    while (frame.status == tnfsFrameStatus::COMPLETE && kind != tnfsReplyKind::ANSWER)
    {
        Debug_printf("Discarding %s TNFS reply: seq %x, cmd %x, result %x\r\n",
                     kind == tnfsReplyKind::EARLIER ? "an earlier" : "a sessionless", out[2], out[3], out[4]);
        if (replies->unsettled() != tnfsOptionalField::NONE && !infers())
        {
            // Only right after an answer can a fence settle it; a fresh stream is safe.
            Debug_printf("Unsettled %s after an earlier reply; starting a new connection\r\n",
                         field_name(replies->unsettled()));
            drop();
            return {tnfsStreamStatus::LOST, 0};
        }
        frame = replies->take(out, ctx, kind);
    }
    if (frame.status == tnfsFrameStatus::COMPLETE)
    {
        // The answer stands either way: no field was pending when it was framed.
        settle();
        return {tnfsStreamStatus::REPLY, frame.length};
    }
    if (frame.status == tnfsFrameStatus::MALFORMED)
    {
        Debug_println("Malformed TNFS reply; dropping the connection");
        drop();
        return {tnfsStreamStatus::LOST, 0};
    }
    if (want == 0 && !_transport.is_open())
    {
        Debug_println("TNFS stream connection closed");
        drop();
        return {tnfsStreamStatus::LOST, 0};
    }
    return {tnfsStreamStatus::PENDING, 0};
}

// Settle a field of unknown use after the answer before anything else is sent, so it cannot
// be read as the next answer: with a fence (IDENTIFY), or else on a new connection.
void tnfsStreamLink::settle()
{
    tnfsReplyAssembler *replies = _mount.tcp_replies.get();
    tnfsOptionalField field = replies->unsettled();
    if (field == tnfsOptionalField::NONE || infers())
        return;
    // No fence while a retry's duplicate may still arrive, nor on a closed connection (the
    // field went with it).
    if (_mount.tcp_format_policy == tnfsFormatPolicy::IDENTIFY && !_mount.tcp_fence_failed &&
        !replies->replies_pending() && _transport.is_open())
    {
        // CLOSE: no optional field in any format; session 0: no session to act on.
        uint8_t sequence = _mount.current_sequence_num++;
        const uint8_t fence[TNFS_HEADER_SIZE + 1] = {0, 0, sequence, TNFS_CMD_CLOSE, 0xFF};
        const tnfsReplyContext ctx = {0, sequence, TNFS_CMD_CLOSE};
        replies->request_sent(sequence, static_cast<int>(_transport.available()));
        success_is_true sent(_transport.write(fence, sizeof(fence)) == sizeof(fence));
        auto ms_start = fnSystem.millis(); // unsigned: the difference survives a wrap
        while (sent.is_success() && fnSystem.millis() - ms_start < static_cast<decltype(ms_start)>(_mount.timeout_ms))
        {
            uint8_t buf[16]; // the fence's answer is 5 bytes; a field is read in a few passes
            size_t want = std::min({_transport.available(), static_cast<size_t>(replies->space()), sizeof(buf)});
            if (want > 0)
                replies->append(buf, _transport.read(buf, want));
            tnfsFenceVerdict verdict = replies->settle(ctx);
            if (verdict == tnfsFenceVerdict::ABSENT || verdict == tnfsFenceVerdict::PRESENT)
            {
                bool present = verdict == tnfsFenceVerdict::PRESENT;
                set_field_use(_mount, field, present ? tnfsFieldUse::PRESENT : tnfsFieldUse::ABSENT);
                Debug_printf("TNFS server %s the optional %s\r\n", present ? "sends" : "leaves out",
                             field_name(field));
                return;
            }
            if (verdict == tnfsFenceVerdict::FAILED || (want == 0 && !_transport.is_open()))
                break;
            if (want == 0)
            {
#ifdef ESP_PLATFORM
                fnSystem.yield();
#else
                fnSystem.delay_microseconds(2000);
#endif
            }
        }
        // Sent but not answered as a fence: stop using fences with this server. A failed
        // write says nothing about the server.
        if (sent.is_success())
        {
            Debug_printf("TNFS fence for the %s failed; reconnecting after such replies\r\n", field_name(field));
            _mount.tcp_fence_failed = true;
        }
    }
    Debug_printf("Unsettled %s; starting a new connection\r\n", field_name(field));
    drop();
}

void tnfsStreamLink::expire(const tnfsPacket &request)
{
    // The retry's reply must not complete a reply that was cut off: start a new stream.
    if (_mount.tcp_replies && _mount.tcp_replies->may_hold_cut_answer(context(request)))
        drop();
}

void tnfsStreamLink::drop()
{
    _transport.close();
    _mount.tcp_replies.reset();
}
