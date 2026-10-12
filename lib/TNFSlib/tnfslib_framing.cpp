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
constexpr int DATA_UNKNOWN = -1;    // not a command this client sends
constexpr int DATA_VARIABLE = -2;   // length comes from the payload itself

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

// Data bytes that follow a successful status for each command FujiNet sends.
int success_data(uint8_t command)
{
    switch (command)
    {
    case TNFS_CMD_UNMOUNT:
    case TNFS_CMD_CLOSEDIR:
    case TNFS_CMD_MKDIR:
    case TNFS_CMD_RMDIR:
    case TNFS_CMD_SEEKDIR:
    case TNFS_CMD_CLOSE:
    case TNFS_CMD_UNLINK:
    case TNFS_CMD_CHMOD:
    case TNFS_CMD_RENAME:
        return 0;
    case TNFS_CMD_OPENDIR:
    case TNFS_CMD_OPEN:
        return 1; // handle
    case TNFS_CMD_WRITE:
        return 2; // bytes written
    case TNFS_CMD_OPENDIRX:
        return 3; // handle (1), entry count (2)
    case TNFS_CMD_MOUNT:
        return 4;          // version (2), minimum retry time (2)
    case TNFS_CMD_LSEEK:   // resulting position, protocol > 1.0 (FujiNet requires 1.2)
    case TNFS_CMD_TELLDIR: // position
    case TNFS_CMD_SIZE:    // kilobytes
    case TNFS_CMD_FREE:    // kilobytes
        return 4;
    case TNFS_CMD_SIZE_BYTES:
    case TNFS_CMD_FREE_BYTES:
        return 8;
    case TNFS_CMD_STAT:
        return 22; // fixed stat block; TNFSD sends no optional uid/gid strings
    case TNFS_CMD_READ:
    case TNFS_CMD_READDIR:
    case TNFS_CMD_READDIRX:
        return DATA_VARIABLE;
    default:
        return DATA_UNKNOWN;
    }
}

bool is_known_status(uint8_t status)
{ return status <= TNFS_RESULT_END_OF_FILE || status == TNFS_RESULT_INVALID_HANDLE; }

// Offset just past the NUL that ends the string starting at `start`; 0 if not yet
// received, -1 if no NUL can occur within a maximum-sized reply.
int end_of_string(const uint8_t *buf, int have, int start)
{
    int limit = std::min(have, TNFS_MAX_REPLY_SIZE);
    for (int i = start; i < limit; i++)
    {
        if (buf[i] == '\0')
            return i + 1;
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
} // namespace

tnfsFrame tnfs_frame_reply(const uint8_t *buf, int have)
{
    if (buf == nullptr || have < REPLY_PREFIX)
        return need_more();

    uint8_t command = buf[OFFSET_COMMAND];
    uint8_t status = buf[OFFSET_STATUS];
    int data = success_data(command);
    if (data == DATA_UNKNOWN || !is_known_status(status))
        return malformed();

    if (status == TNFS_RESULT_TRY_AGAIN)
        return expect(REPLY_PREFIX + TRY_AGAIN_DATA, have);
    if (status != TNFS_RESULT_SUCCESS)
    {
        int error_data = command == TNFS_CMD_MOUNT ? MOUNT_ERROR_DATA : 0;
        return expect(REPLY_PREFIX + error_data, have);
    }

    switch (command)
    {
    case TNFS_CMD_READ:
        return frame_read(buf, have);
    case TNFS_CMD_READDIR:
        return frame_readdir(buf, have);
    case TNFS_CMD_READDIRX:
        return frame_readdirx(buf, have);
    default:
        return expect(REPLY_PREFIX + data, have);
    }
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
