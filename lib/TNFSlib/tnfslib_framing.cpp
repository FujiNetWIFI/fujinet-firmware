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
    return frame;
}
