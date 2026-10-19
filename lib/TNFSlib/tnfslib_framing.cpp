#include "tnfslib_framing.h"

#include <algorithm>
#include <cstring>

#include "global_types.h"

// Every reply: session id (2), sequence (1), command (1), status (1), then data.
// The sizes below are the data after the status byte; see tnfs-protocol.md and TNFSD.

namespace
{
constexpr int REPLY_PREFIX = TNFS_HEADER_SIZE + 1; // header + status byte
constexpr int OFFSET_COMMAND = 3;
constexpr int OFFSET_STATUS = 4;

constexpr int TRY_AGAIN_DATA = 2;   // EAGAIN: back-off in ms, any command
constexpr int MOUNT_ERROR_DATA = 2; // failed MOUNT still carries the server version
constexpr int READ_PREFIX = 2;      // byte count before the data
constexpr int READDIRX_PREFIX = 4;  // entry count, dir status, dir position (2)
constexpr int READDIRX_FIXED = 13;  // flags (1), size (4), mtime (4), ctime (4), then name

tnfsFrame need_more() { return {tnfsFrameStatus::NEED_MORE, 0}; }

tnfsFrame malformed() { return {tnfsFrameStatus::MALFORMED, 0}; }

// A reply of `length` bytes: complete once `have` covers it, malformed if it cannot fit.
tnfsFrame expect(int length, int have)
{
    if (length > TNFS_MAX_REPLY_SIZE)
        return malformed();
    if (have < length)
        return need_more();
    return {tnfsFrameStatus::COMPLETE, length};
}

bool is_known_status(uint8_t status)
{ return status <= TNFS_RESULT_END_OF_FILE || status == TNFS_RESULT_INVALID_HANDLE; }

// Offset just past the NUL that ends the string starting at `start`; 0 if not yet
// received, -1 if no NUL can occur within a maximum-sized reply.
int end_of_string(const uint8_t *buf, int have, int start)
{
    // Not strlen(): the terminator may not have arrived yet.
    int limit = std::min(have, TNFS_MAX_REPLY_SIZE);
    if (start < limit)
    {
        const void *nul = std::memchr(buf + start, '\0', limit - start);
        if (nul != nullptr)
            return static_cast<int>(static_cast<const uint8_t *>(nul) - buf) + 1;
    }
    return have >= TNFS_MAX_REPLY_SIZE ? -1 : 0;
}

tnfsFrame frame_read(const uint8_t *buf, int have)
{
    if (have < REPLY_PREFIX + READ_PREFIX)
        return need_more();
    u16le_t count;
    std::memcpy(&count, buf + REPLY_PREFIX, sizeof(count));
    return expect(REPLY_PREFIX + READ_PREFIX + static_cast<uint16_t>(count), have);
}

tnfsFrame frame_readdir(const uint8_t *buf, int have)
{
    int end = end_of_string(buf, have, REPLY_PREFIX);
    if (end < 0)
        return malformed();
    if (end == 0)
        return need_more();
    return {tnfsFrameStatus::COMPLETE, end};
}

tnfsFrame frame_readdirx(const uint8_t *buf, int have)
{
    int offset = REPLY_PREFIX + READDIRX_PREFIX;
    if (have < offset)
        return need_more();
    int entries = buf[REPLY_PREFIX];
    for (int e = 0; e < entries; e++)
    {
        offset += READDIRX_FIXED;
        if (offset >= TNFS_MAX_REPLY_SIZE)
            return malformed(); // no room left for even an empty name
        if (have <= offset)
            return need_more();
        int end = end_of_string(buf, have, offset);
        if (end < 0)
            return malformed();
        if (end == 0)
            return need_more();
        offset = end;
    }
    return {tnfsFrameStatus::COMPLETE, offset};
}

// The successful reply to each command FujiNet sends: `data` bytes after the status, or,
// when the length is in the payload, where `frame` finds its end.
struct tnfsReplyShape
{
    uint8_t command;
    int data;                                         // bytes after the status byte
    tnfsFrame (*frame)(const uint8_t *buf, int have); // nullptr: fixed length
};

constexpr tnfsReplyShape REPLY_SHAPES[] = {
    {TNFS_CMD_MOUNT, 4, nullptr},    // version (2), minimum retry time (2)
    {TNFS_CMD_UNMOUNT, 0, nullptr},
    {TNFS_CMD_OPENDIR, 1, nullptr},  // handle
    {TNFS_CMD_READDIR, 0, frame_readdir},
    {TNFS_CMD_CLOSEDIR, 0, nullptr},
    {TNFS_CMD_MKDIR, 0, nullptr},
    {TNFS_CMD_RMDIR, 0, nullptr},
    {TNFS_CMD_TELLDIR, 4, nullptr},  // position
    {TNFS_CMD_SEEKDIR, 0, nullptr},
    {TNFS_CMD_OPENDIRX, 3, nullptr}, // handle (1), entry count (2)
    {TNFS_CMD_READDIRX, 0, frame_readdirx},
    {TNFS_CMD_READ, 0, frame_read},
    {TNFS_CMD_WRITE, 2, nullptr},    // bytes written
    {TNFS_CMD_CLOSE, 0, nullptr},
    {TNFS_CMD_STAT, 22, nullptr},    // fixed stat block, no uid/gid strings (see the header)
    {TNFS_CMD_LSEEK, 4, nullptr},    // resulting position; protocol > 1.0 (FujiNet needs 1.2)
    {TNFS_CMD_UNLINK, 0, nullptr},
    {TNFS_CMD_CHMOD, 0, nullptr},
    {TNFS_CMD_RENAME, 0, nullptr},
    {TNFS_CMD_OPEN, 1, nullptr},     // handle
    {TNFS_CMD_SIZE, 4, nullptr},     // kilobytes
    {TNFS_CMD_FREE, 4, nullptr},     // kilobytes
    {TNFS_CMD_SIZE_BYTES, 8, nullptr},
    {TNFS_CMD_FREE_BYTES, 8, nullptr},
};

constexpr bool one_row_per_command()
{
    for (const auto &a : REPLY_SHAPES)
    {
        int rows = 0;
        for (const auto &b : REPLY_SHAPES)
            rows += a.command == b.command;
        if (rows != 1)
            return false;
    }
    return true;
}
static_assert(one_row_per_command(), "REPLY_SHAPES lists a command twice");

// The row for `command`, or nullptr for a command this client never sends.
const tnfsReplyShape *reply_shape(uint8_t command)
{
    for (const auto &row : REPLY_SHAPES)
    {
        if (row.command == command)
            return &row;
    }
    return nullptr;
}
} // namespace

tnfsFrame tnfs_frame_reply(const uint8_t *buf, int have)
{
    if (buf == nullptr || have < REPLY_PREFIX)
        return need_more();

    uint8_t command = buf[OFFSET_COMMAND];
    uint8_t status = buf[OFFSET_STATUS];
    const tnfsReplyShape *shape = reply_shape(command);
    if (shape == nullptr || !is_known_status(status))
        return malformed();

    if (status == TNFS_RESULT_TRY_AGAIN)
        return expect(REPLY_PREFIX + TRY_AGAIN_DATA, have);
    if (status != TNFS_RESULT_SUCCESS)
    {
        int error_data = command == TNFS_CMD_MOUNT ? MOUNT_ERROR_DATA : 0;
        return expect(REPLY_PREFIX + error_data, have);
    }

    if (shape->frame != nullptr)
        return shape->frame(buf, have);
    return expect(REPLY_PREFIX + shape->data, have);
}

int tnfsReplyAssembler::append(const uint8_t *data, int len)
{
    if (data == nullptr || len <= 0)
        return 0;
    int accepted = std::min(len, space());
    std::memcpy(_buf + _len, data, accepted);
    _len += accepted;
    return accepted;
}

tnfsFrame tnfsReplyAssembler::take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE])
{
    tnfsFrame frame = tnfs_frame_reply(_buf, _len);
    if (frame.status == tnfsFrameStatus::MALFORMED)
    {
        reset();
        return frame;
    }
    if (frame.status == tnfsFrameStatus::NEED_MORE)
        return frame;

    std::memcpy(out, _buf, frame.length);
    _len -= frame.length;
    std::memmove(_buf, _buf + frame.length, _len);
    _stream_pos += frame.length;
    return frame;
}

namespace
{
constexpr int OFFSET_SEQUENCE = 2;
constexpr int STAT_BLOCK = 22;
// The strings are part of the STAT reply, so they fit in what a maximum reply leaves.
constexpr int MAX_STAT_STRINGS = TNFS_MAX_REPLY_SIZE - REPLY_PREFIX - STAT_BLOCK;

// How a reading of the stream ranks; a reading still arriving outranks all but an answer.
enum class tnfsReading
{
    NONE,
    SESSIONLESS,
    EARLIER,
    MORE,
    ANSWER,
};

// Which kinds of reply the header bytes that have arrived still allow.
struct tnfsHeaderFit
{
    bool answer;
    bool own_answer; // an answer carrying our session (or any, for MOUNT)
    bool earlier;
    bool sessionless;
    bool complete; // the whole header and status are here
};

bool is_sessionless_status(uint8_t status)
{ return status == TNFS_RESULT_INVALID_HANDLE || status == TNFS_RESULT_NOT_PERMITTED; }

// `may_answer`: false for bytes received before the request was sent.
tnfsHeaderFit fit_header(const uint8_t *p, int have, const tnfsReplyContext &c, bool may_answer)
{
    bool own = true, zero = true;
    if (have > 0)
    {
        own = p[0] == (c.session & 0xFF);
        zero = p[0] == 0;
    }
    if (have > 1)
    {
        own = own && p[1] == (c.session >> 8);
        zero = zero && p[1] == 0;
    }
    bool sequence_known = have > OFFSET_SEQUENCE;
    bool our_sequence = !sequence_known || p[OFFSET_SEQUENCE] == c.sequence;
    bool other_sequence = !sequence_known || p[OFFSET_SEQUENCE] != c.sequence;
    bool command_known = have > OFFSET_COMMAND;
    bool our_command = !command_known || p[OFFSET_COMMAND] == c.command;
    bool framed_command = !command_known || reply_shape(p[OFFSET_COMMAND]) != nullptr;
    bool status_known = have > OFFSET_STATUS;
    bool known_status = !status_known || is_known_status(p[OFFSET_STATUS]);
    bool sessionless_status = !status_known || is_sessionless_status(p[OFFSET_STATUS]);
    bool mount = c.command == TNFS_CMD_MOUNT;

    tnfsHeaderFit fit;
    fit.own_answer = may_answer && our_sequence && our_command && known_status && (mount || own);
    // An expired session is reported with session 0.
    fit.answer = fit.own_answer ||
                 (may_answer && our_sequence && our_command && zero && sessionless_status);
    // Bytes from before the request went out, even with its sequence, answer an earlier one.
    fit.earlier = own && (other_sequence || !may_answer) && framed_command && known_status;
    fit.sessionless = zero && c.session != 0 && !mount && sessionless_status;
    fit.complete = have >= REPLY_PREFIX;
    return fit;
}

tnfsReading rank(const tnfsHeaderFit &fit)
{
    if (!fit.complete)
        return fit.answer || fit.earlier || fit.sessionless ? tnfsReading::MORE : tnfsReading::NONE;
    if (fit.answer)
        return tnfsReading::ANSWER;
    if (fit.earlier)
        return tnfsReading::EARLIER;
    return fit.sessionless ? tnfsReading::SESSIONLESS : tnfsReading::NONE;
}

// Length of `field` at the front: > 0 when it is there, 0 when it could still be arriving,
// -1 when these bytes cannot be it.
int optional_length(const uint8_t *p, int have, tnfsOptionalField field)
{
    if (field == tnfsOptionalField::STAT_STRINGS)
    {
        int nuls = 0;
        for (int i = 0; i < std::min(have, MAX_STAT_STRINGS); i++)
            if (p[i] == '\0' && ++nuls == 2)
                return i + 1;
        return have >= MAX_STAT_STRINGS ? -1 : 0;
    }
    if (field == tnfsOptionalField::READ_COUNT || field == tnfsOptionalField::WRITE_COUNT)
    {
        for (int i = 0; i < std::min(have, 2); i++)
            if (p[i] != 0)
                return -1;
        return have >= 2 ? 2 : 0;
    }
    return -1;
}

// The optional field a reply may still have after it. paumaed's WRITE leaves its count out
// of INVALID, and no server is known to send one there.
tnfsOptionalField optional_after(const uint8_t *reply)
{
    uint8_t command = reply[OFFSET_COMMAND];
    uint8_t status = reply[OFFSET_STATUS];
    if (command == TNFS_CMD_STAT && status == TNFS_RESULT_SUCCESS)
        return tnfsOptionalField::STAT_STRINGS;
    if (status == TNFS_RESULT_SUCCESS || status == TNFS_RESULT_TRY_AGAIN)
        return tnfsOptionalField::NONE;
    if (command == TNFS_CMD_READ)
        return tnfsOptionalField::READ_COUNT;
    if (command == TNFS_CMD_WRITE && status != TNFS_RESULT_INVALID_HANDLE)
        return tnfsOptionalField::WRITE_COUNT;
    return tnfsOptionalField::NONE;
}

tnfsFieldUse field_use(const tnfsReplyFormat &format, tnfsOptionalField field)
{
    switch (field)
    {
    case tnfsOptionalField::STAT_STRINGS:
        return format.stat_strings;
    case tnfsOptionalField::READ_COUNT:
        return format.read_count;
    case tnfsOptionalField::WRITE_COUNT:
        return format.write_count;
    default:
        return tnfsFieldUse::ABSENT;
    }
}

// The reply at the front, with the optional field that `format` says the server sends.
tnfsFrame frame_in_format(const uint8_t *p, int have, const tnfsReplyFormat &format)
{
    tnfsFrame frame = tnfs_frame_reply(p, have);
    if (frame.status != tnfsFrameStatus::COMPLETE)
        return frame;
    tnfsOptionalField field = optional_after(p);
    if (field_use(format, field) != tnfsFieldUse::PRESENT)
        return frame;
    int extra = optional_length(p + frame.length, have - frame.length, field);
    if (extra < 0)
        return malformed();
    if (extra == 0)
        return need_more();
    return {tnfsFrameStatus::COMPLETE, frame.length + extra};
}

// Whether the start of an answer to `c`, carrying our session, could be at the front.
bool own_answer_starts(const uint8_t *p, int have, const tnfsReplyContext &c, bool may_answer)
{
    tnfsHeaderFit fit = fit_header(p, have, c, may_answer);
    return have > 0 && fit.own_answer;
}
} // namespace

tnfsFrame tnfsReplyAssembler::take(uint8_t (&out)[TNFS_MAX_REPLY_SIZE],
                                   const tnfsReplyContext &context, tnfsReplyKind &kind)
{
    // Up to two readings: a reply at the front, or the previous reply's optional field
    // followed by a reply.
    int offsets[2] = {0, 0};
    tnfsReading readings[2] = {rank(fit_header(_buf, _len, context, after_request(0, context))),
                               tnfsReading::NONE};
    int count = 1;
    if (_optional != tnfsOptionalField::NONE)
    {
        int field = optional_length(_buf, _len, _optional);
        if (field > 0)
        {
            offsets[1] = field;
            readings[1] = rank(fit_header(_buf + field, _len - field, context,
                                          after_request(field, context)));
            count = 2;
        }
        else if (field == 0)
        {
            readings[1] = tnfsReading::MORE;
            count = 2;
        }
    }

    int best = readings[1] > readings[0] && count == 2 ? 1 : 0;
    tnfsReading reading = readings[best];
    if (reading == tnfsReading::MORE)
        return need_more();
    bool tie = count == 2 && readings[0] == readings[1];
    if (reading == tnfsReading::NONE || tie)
    {
        reset();
        return malformed();
    }

    const uint8_t *front = _buf + offsets[best];
    int have = _len - offsets[best];
    tnfsFrame frame = reading == tnfsReading::SESSIONLESS ? expect(REPLY_PREFIX, have)
                                                         : frame_in_format(front, have, context.format);
    if (frame.status == tnfsFrameStatus::NEED_MORE)
        return frame;
    if (frame.status == tnfsFrameStatus::MALFORMED)
    {
        reset();
        return frame;
    }

    std::memcpy(out, front, frame.length);
    int used = offsets[best] + frame.length;
    _len -= used;
    std::memmove(_buf, _buf + used, _len);
    _stream_pos += used;
    // A field the format leaves UNKNOWN stays to be settled; any other was framed above.
    _optional = tnfsOptionalField::NONE;
    if (reading != tnfsReading::SESSIONLESS &&
        field_use(context.format, optional_after(out)) == tnfsFieldUse::UNKNOWN)
        _optional = optional_after(out);
    kind = reading == tnfsReading::ANSWER    ? tnfsReplyKind::ANSWER
           : reading == tnfsReading::EARLIER ? tnfsReplyKind::EARLIER
                                             : tnfsReplyKind::SESSIONLESS;
    if (kind != tnfsReplyKind::SESSIONLESS)
        _outstanding--;
    return frame;
}

bool tnfsReplyAssembler::may_hold_cut_answer(const tnfsReplyContext &context) const
{
    // A header of ours with its data still missing, or the start of such a header: the
    // retry's reply would supply the rest. Other bytes it would only misframe, which
    // take() then reports as MALFORMED.
    auto cut = [&](int offset)
    {
        const uint8_t *p = _buf + offset;
        int have = _len - offset;
        bool may_answer = after_request(offset, context);
        if (have < REPLY_PREFIX)
            return own_answer_starts(p, have, context, may_answer);
        return fit_header(p, have, context, may_answer).answer &&
               frame_in_format(p, have, context.format).status == tnfsFrameStatus::NEED_MORE;
    };
    if (_len == 0)
        return false;
    if (cut(0))
        return true;
    int field = optional_length(_buf, _len, _optional);
    return field > 0 && field < _len && cut(field);
}

tnfsFenceVerdict tnfsReplyAssembler::settle(const tnfsReplyContext &fence)
{
    if (_optional == tnfsOptionalField::NONE)
        return tnfsFenceVerdict::FAILED;
    // 1: the fence's answer is at `offset`; 0: it could still be; -1: it cannot be.
    auto answer_at = [&](int offset)
    {
        if (!after_request(offset, fence))
            return -1; // received before the fence went out
        const uint8_t head[TNFS_HEADER_SIZE] = {0, 0, fence.sequence, fence.command};
        int have = _len - offset;
        for (int i = 0; i < std::min(have, TNFS_HEADER_SIZE); i++)
            if (_buf[offset + i] != head[i])
                return -1;
        if (have < REPLY_PREFIX)
            return 0;
        uint8_t status = _buf[offset + OFFSET_STATUS];
        return is_known_status(status) && status != TNFS_RESULT_TRY_AGAIN ? 1 : -1;
    };
    int absent = answer_at(0);
    int field = optional_length(_buf, _len, _optional);
    int present = field > 0 ? answer_at(field) : field;
    tnfsFenceVerdict verdict = tnfsFenceVerdict::NEED_MORE;
    int used = 0;
    if (absent < 0 && present < 0)
        verdict = tnfsFenceVerdict::FAILED;
    else if (absent > 0 && present < 0)
        verdict = tnfsFenceVerdict::ABSENT, used = REPLY_PREFIX;
    else if (present > 0 && absent < 0)
        verdict = tnfsFenceVerdict::PRESENT, used = field + REPLY_PREFIX;
    if (verdict == tnfsFenceVerdict::FAILED)
        reset();
    else if (used > 0)
    {
        _len -= used;
        std::memmove(_buf, _buf + used, _len);
        _stream_pos += used;
        _optional = tnfsOptionalField::NONE;
        _outstanding--; // the fence's answer
    }
    return verdict;
}

void tnfsReplyAssembler::request_sent(uint8_t sequence, int unread)
{
    _outstanding++;
    if (_sent_marked && _sent_sequence == sequence)
        return; // a retry: its answer may already have begun before it went out
    _answer_from = _stream_pos + static_cast<uint64_t>(_len) +
                   static_cast<uint64_t>(std::max(unread, 0));
    _sent_sequence = sequence;
    _sent_marked = true;
}
