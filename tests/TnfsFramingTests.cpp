#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <random>
#include <string>
#include <vector>

#include "tnfslib_framing.h"

using Bytes = std::vector<uint8_t>;

// Reply header (session 0xBEEF, little endian) + status + data.
static Bytes reply(uint8_t command, uint8_t status, const Bytes &data = {}, uint8_t seq = 1)
{
    Bytes r = {0xEF, 0xBE, seq, command, status};
    r.insert(r.end(), data.begin(), data.end());
    return r;
}

static Bytes zeros(int n) { return Bytes(n, 0); }

static Bytes read_reply(int count, uint8_t fill = 0xA5)
{
    Bytes data = {static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>(count >> 8)};
    data.insert(data.end(), count, fill);
    return reply(TNFS_CMD_READ, TNFS_RESULT_SUCCESS, data);
}

// READDIRX reply with one entry per name: flags, size, mtime, ctime, name + NUL.
static Bytes readdirx_reply(const std::vector<std::string> &names)
{
    Bytes data = {static_cast<uint8_t>(names.size()), 0x00, 0x00, 0x00};
    for (const auto &name : names)
    {
        Bytes fixed = zeros(13);
        data.insert(data.end(), fixed.begin(), fixed.end());
        data.insert(data.end(), name.begin(), name.end());
        data.push_back('\0');
    }
    return reply(TNFS_CMD_READDIRX, TNFS_RESULT_SUCCESS, data);
}

static tnfsFrame frame(const Bytes &b)
{ return tnfs_frame_reply(b.data(), static_cast<int>(b.size())); }

static void check_complete(const Bytes &b)
{
    tnfsFrame f = frame(b);
    CHECK(f.status == tnfsFrameStatus::COMPLETE);
    CHECK(f.length == static_cast<int>(b.size()));
    // One byte short of the reply is never enough.
    tnfsFrame s = tnfs_frame_reply(b.data(), static_cast<int>(b.size()) - 1);
    CHECK(s.status == tnfsFrameStatus::NEED_MORE);
}

// Feed a byte stream to an assembler in the given chunk sizes and collect every reply.
static std::vector<Bytes> reassemble(const Bytes &stream, const std::vector<int> &chunks)
{
    tnfsReplyAssembler a;
    std::vector<Bytes> out;
    uint8_t buf[TNFS_MAX_REPLY_SIZE];
    size_t pos = 0, c = 0;
    while (pos < stream.size() || a.buffered() > 0)
    {
        if (pos < stream.size())
        {
            int want = std::min<int>(chunks[c++ % chunks.size()], stream.size() - pos);
            pos += a.append(stream.data() + pos, want);
        }
        for (;;)
        {
            tnfsFrame f = a.take(buf);
            if (f.status != tnfsFrameStatus::COMPLETE)
            {
                REQUIRE(f.status == tnfsFrameStatus::NEED_MORE);
                break;
            }
            out.emplace_back(buf, buf + f.length);
        }
        if (pos >= stream.size() && a.buffered() > 0)
            break; // trailing partial reply; a caller would wait or time out
    }
    return out;
}

static std::vector<Bytes> sample_session()
{
    return {
        reply(TNFS_CMD_MOUNT, TNFS_RESULT_SUCCESS, {0x03, 0x01, 0xE8, 0x03}),
        reply(TNFS_CMD_OPENDIRX, TNFS_RESULT_SUCCESS, {0x07, 0x03, 0x00}),
        readdirx_reply({"spy1.atr", "spy2.atr", "Juegos_Calaluis"}),
        reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x02}),
        read_reply(512),
        reply(TNFS_CMD_READ, TNFS_RESULT_TRY_AGAIN, {0xE8, 0x03}),
        read_reply(525, 0x5A),
        reply(TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE),
        reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, zeros(22)),
        reply(TNFS_CMD_READDIR, TNFS_RESULT_SUCCESS, {'a', 'b', 'c', '\0'}),
        reply(TNFS_CMD_SIZE_BYTES, TNFS_RESULT_SUCCESS, zeros(8)),
        reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS),
        reply(TNFS_CMD_UNMOUNT, TNFS_RESULT_SUCCESS),
    };
}

static Bytes concat(const std::vector<Bytes> &replies)
{
    Bytes s;
    for (const auto &r : replies)
        s.insert(s.end(), r.begin(), r.end());
    return s;
}

TEST_CASE("fixed-size success replies have the documented length")
{
    const std::vector<std::pair<uint8_t, int>> table = {
        {TNFS_CMD_MOUNT, 4},    {TNFS_CMD_UNMOUNT, 0},    {TNFS_CMD_OPENDIR, 1},
        {TNFS_CMD_CLOSEDIR, 0}, {TNFS_CMD_MKDIR, 0},      {TNFS_CMD_RMDIR, 0},
        {TNFS_CMD_TELLDIR, 4},  {TNFS_CMD_SEEKDIR, 0},    {TNFS_CMD_OPENDIRX, 3},
        {TNFS_CMD_WRITE, 2},    {TNFS_CMD_CLOSE, 0},      {TNFS_CMD_STAT, 22},
        {TNFS_CMD_LSEEK, 4},    {TNFS_CMD_UNLINK, 0},     {TNFS_CMD_CHMOD, 0},
        {TNFS_CMD_RENAME, 0},   {TNFS_CMD_OPEN, 1},       {TNFS_CMD_SIZE, 4},
        {TNFS_CMD_FREE, 4},     {TNFS_CMD_SIZE_BYTES, 8}, {TNFS_CMD_FREE_BYTES, 8},
    };
    for (const auto &[command, data] : table)
    {
        CAPTURE(command);
        check_complete(reply(command, TNFS_RESULT_SUCCESS, zeros(data)));
    }
}

TEST_CASE("error replies carry only the status, except MOUNT and EAGAIN")
{
    const uint8_t commands[] = {TNFS_CMD_UNMOUNT,  TNFS_CMD_OPENDIR,   TNFS_CMD_READDIR,
                                TNFS_CMD_OPENDIRX, TNFS_CMD_READDIRX,  TNFS_CMD_READ,
                                TNFS_CMD_WRITE,    TNFS_CMD_STAT,      TNFS_CMD_LSEEK,
                                TNFS_CMD_OPEN,     TNFS_CMD_SIZE_BYTES};
    for (uint8_t command : commands)
    {
        CAPTURE(command);
        check_complete(reply(command, TNFS_RESULT_FILE_NOT_FOUND));
        check_complete(reply(command, TNFS_RESULT_END_OF_FILE));
        check_complete(reply(command, TNFS_RESULT_INVALID_HANDLE));
        // EAGAIN is followed by a 16-bit back-off for every command (tnfs-protocol.md).
        check_complete(reply(command, TNFS_RESULT_TRY_AGAIN, {0xE8, 0x03}));
    }
    // A failed MOUNT still carries the server version (spec example, version 3.5).
    check_complete({0x00, 0x00, 0x00, 0x00, 0x1F, 0x05, 0x03});
    check_complete(reply(TNFS_CMD_MOUNT, TNFS_RESULT_TRY_AGAIN, {0x10, 0x00}));
}

TEST_CASE("replies captured from TNFSD 1.3 and the protocol spec")
{
    // Live Q4 capture through the Cloudflare tunnel (FUJINET_WSS_DESKTOP_VALIDATION.md).
    check_complete({0x7d, 0xed, 0x00, 0x00, 0x00, 0x03, 0x01, 0xe8, 0x03});
    check_complete({0x7d, 0xed, 0x01, 0x24, 0x00, 0xed, 0x41, 0xe9, 0x03,
                    0xe9, 0x03, 0x00, 0x10, 0x00, 0x00, 0x1f, 0x9a, 0x4f,
                    0x6a, 0xbe, 0x72, 0x66, 0x6a, 0xbe, 0x72, 0x66, 0x6a});
    check_complete({0x7d, 0xed, 0x02, 0x01, 0x00});
    // Spec: MOUNT success, server version 2.6, retry 5000 ms, session 0xBEEF.
    check_complete({0xEF, 0xBE, 0x00, 0x00, 0x00, 0x06, 0x02, 0x88, 0x13});
}

TEST_CASE("READ length comes from its byte count and is bounded")
{
    check_complete(read_reply(0));
    check_complete(read_reply(1));
    check_complete(read_reply(512));
    check_complete(read_reply(TNFS_MAX_REPLY_SIZE - 7)); // largest that fits: 525
    CHECK(frame(read_reply(TNFS_MAX_REPLY_SIZE - 6)).status == tnfsFrameStatus::MALFORMED);
    CHECK(frame(read_reply(0xFFFF)).status == tnfsFrameStatus::MALFORMED);
    // Count not yet received.
    Bytes head = reply(TNFS_CMD_READ, TNFS_RESULT_SUCCESS, {0x10});
    CHECK(frame(head).status == tnfsFrameStatus::NEED_MORE);
}

TEST_CASE("READDIR ends at the name terminator")
{
    check_complete(reply(TNFS_CMD_READDIR, TNFS_RESULT_SUCCESS, {'\0'}));
    check_complete(reply(TNFS_CMD_READDIR, TNFS_RESULT_SUCCESS, {'d', 'i', 'r', '\0'}));
    Bytes no_nul = reply(TNFS_CMD_READDIR, TNFS_RESULT_SUCCESS, Bytes(600, 'x'));
    CHECK(frame(no_nul).status == tnfsFrameStatus::MALFORMED);
    Bytes partial = reply(TNFS_CMD_READDIR, TNFS_RESULT_SUCCESS, Bytes(100, 'x'));
    CHECK(frame(partial).status == tnfsFrameStatus::NEED_MORE);
}

TEST_CASE("READDIRX walks every entry and is bounded")
{
    check_complete(readdirx_reply({}));
    check_complete(readdirx_reply({""}));
    check_complete(readdirx_reply({"a", "bb", "ccc", "spy1.atr"}));
    // Partial: stop inside the second name.
    Bytes r = readdirx_reply({"first", "second"});
    CHECK(tnfs_frame_reply(r.data(), static_cast<int>(r.size()) - 3).status ==
          tnfsFrameStatus::NEED_MORE);
    // An entry count that cannot fit in one reply.
    Bytes huge = reply(TNFS_CMD_READDIRX, TNFS_RESULT_SUCCESS, {0xFF, 0x00, 0x00, 0x00});
    while (huge.size() < 2 * TNFS_MAX_REPLY_SIZE)
        huge.push_back(0x00);
    CHECK(frame(huge).status == tnfsFrameStatus::MALFORMED);
}

TEST_CASE("headers shorter than header plus status need more bytes")
{
    Bytes r = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS);
    for (int have = 0; have < 5; have++)
        CHECK(tnfs_frame_reply(r.data(), have).status == tnfsFrameStatus::NEED_MORE);
    CHECK(tnfs_frame_reply(nullptr, 10).status == tnfsFrameStatus::NEED_MORE);
}

TEST_CASE("unknown commands and status codes are malformed")
{
    CHECK(frame(reply(0x99, TNFS_RESULT_SUCCESS)).status == tnfsFrameStatus::MALFORMED);
    CHECK(frame(reply(TNFS_CMD_SIZE + 0x40, 0x00)).status == tnfsFrameStatus::MALFORMED);
    CHECK(frame(reply(TNFS_CMD_CLOSE, 0x40)).status == tnfsFrameStatus::MALFORMED);
    CHECK(frame(reply(TNFS_CMD_CLOSE, 0xFE)).status == tnfsFrameStatus::MALFORMED);
}

TEST_CASE("fragmented stream: every chunk size yields the same replies")
{
    std::vector<Bytes> expected = sample_session();
    Bytes stream = concat(expected);
    REQUIRE(stream.size() > static_cast<size_t>(tnfsReplyAssembler::CAPACITY));
    for (int chunk = 1; chunk <= 64; chunk++)
    {
        CAPTURE(chunk);
        CHECK(reassemble(stream, {chunk}) == expected);
    }
    CHECK(reassemble(stream, {1, 7, 3, 500, 2, 1063}) == expected);
}

TEST_CASE("fragmented stream: every two-way split point")
{
    std::vector<Bytes> expected = {readdirx_reply({"one", "two"}), read_reply(300),
                                   reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS)};
    Bytes stream = concat(expected);
    for (size_t split = 1; split < stream.size(); split++)
    {
        CAPTURE(split);
        int first = static_cast<int>(split);
        int rest = static_cast<int>(stream.size() - split);
        CHECK(reassemble(stream, {first, rest}) == expected);
    }
}

TEST_CASE("coalesced replies and a late duplicate are taken one at a time")
{
    Bytes dup = reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, zeros(22), 7);
    std::vector<Bytes> expected = {dup, dup,
                                   reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 8)};
    // The framer reports both copies; dropping the stale one is the sequence check's job.
    CHECK(reassemble(concat(expected), {TNFS_MAX_REPLY_SIZE}) == expected);
}

TEST_CASE("assembler: NEED_MORE consumes nothing, reset drops a truncated reply")
{
    tnfsReplyAssembler a;
    uint8_t out[TNFS_MAX_REPLY_SIZE];
    Bytes r = read_reply(400);
    CHECK(a.append(r.data(), 200) == 200);
    CHECK(a.take(out).status == tnfsFrameStatus::NEED_MORE);
    CHECK(a.buffered() == 200);
    a.reset(); // e.g. timeout or reconnect
    CHECK(a.buffered() == 0);
    Bytes close = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS);
    a.append(close.data(), static_cast<int>(close.size()));
    tnfsFrame f = a.take(out);
    CHECK(f.status == tnfsFrameStatus::COMPLETE);
    CHECK(Bytes(out, out + f.length) == close);
}

TEST_CASE("assembler: malformed input is discarded and the next reply frames cleanly")
{
    tnfsReplyAssembler a;
    uint8_t out[TNFS_MAX_REPLY_SIZE];
    Bytes junk = {0x12, 0x34, 0x56, 0x99, 0x00, 0x01, 0x02};
    Bytes close = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS);
    a.append(junk.data(), static_cast<int>(junk.size()));
    a.append(close.data(), static_cast<int>(close.size()));
    CHECK(a.take(out).status == tnfsFrameStatus::MALFORMED);
    CHECK(a.buffered() == 0); // without a length field there is no safe resync point
    a.append(close.data(), static_cast<int>(close.size()));
    CHECK(a.take(out).status == tnfsFrameStatus::COMPLETE);
}

TEST_CASE("assembler: append never overflows and arguments are checked")
{
    tnfsReplyAssembler a;
    Bytes big(3 * tnfsReplyAssembler::CAPACITY, 0x00);
    CHECK(a.append(big.data(), static_cast<int>(big.size())) == tnfsReplyAssembler::CAPACITY);
    CHECK(a.space() == 0);
    CHECK(a.append(big.data(), 10) == 0);
    tnfsReplyAssembler b;
    CHECK(b.append(nullptr, 10) == 0);
    CHECK(b.append(big.data(), -5) == 0);
    CHECK(b.buffered() == 0);
}

TEST_CASE("fuzz: framing invariants hold for arbitrary bytes")
{
    std::mt19937 rng(0x7F5);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<int> len(0, tnfsReplyAssembler::CAPACITY);
    std::vector<Bytes> session = sample_session();
    for (int i = 0; i < 20000; i++)
    {
        Bytes b(len(rng));
        for (auto &x : b)
            x = static_cast<uint8_t>(byte(rng));
        if (i % 2 && !b.empty()) // half the cases start with a valid header
        {
            const Bytes &r = session[i % session.size()];
            std::copy(r.begin(), r.begin() + std::min(r.size(), b.size()), b.begin());
        }
        tnfsFrame f = frame(b);
        if (f.status == tnfsFrameStatus::COMPLETE)
        {
            REQUIRE(f.length >= 5);
            REQUIRE(f.length <= static_cast<int>(b.size()));
            REQUIRE(f.length <= TNFS_MAX_REPLY_SIZE);
        }
        else
        {
            REQUIRE(f.length == 0);
        }
        REQUIRE(frame(b).status == f.status); // deterministic
    }
}
