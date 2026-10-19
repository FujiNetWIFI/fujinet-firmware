#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <string>
#include <utility>
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
    // insert() after a 2-byte init trips a GCC 15/16 -Wstringop-overflow false positive.
    Bytes data(2 + count, fill);
    data[0] = static_cast<uint8_t>(count & 0xFF);
    data[1] = static_cast<uint8_t>(count >> 8);
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
        readdirx_reply({"spy1.atr", "spy2.atr", "Games_Directory"}),
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
    // Captured from a TNFSD 1.3 server: MOUNT, STAT and UMOUNT replies.
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

TEST_CASE("exactly the 24 TNFS commands frame; every other command byte is malformed")
{
    const std::vector<uint8_t> known = {
        TNFS_CMD_MOUNT,    TNFS_CMD_UNMOUNT,  TNFS_CMD_OPENDIR,    TNFS_CMD_READDIR,
        TNFS_CMD_CLOSEDIR, TNFS_CMD_MKDIR,    TNFS_CMD_RMDIR,      TNFS_CMD_TELLDIR,
        TNFS_CMD_SEEKDIR,  TNFS_CMD_OPENDIRX, TNFS_CMD_READDIRX,   TNFS_CMD_READ,
        TNFS_CMD_WRITE,    TNFS_CMD_CLOSE,    TNFS_CMD_STAT,       TNFS_CMD_LSEEK,
        TNFS_CMD_UNLINK,   TNFS_CMD_CHMOD,    TNFS_CMD_RENAME,     TNFS_CMD_OPEN,
        TNFS_CMD_SIZE,     TNFS_CMD_FREE,     TNFS_CMD_SIZE_BYTES, TNFS_CMD_FREE_BYTES};
    for (int command = 0; command < 256; command++)
    {
        CAPTURE(command);
        // An error reply with room for MOUNT's version bytes.
        Bytes r = reply(static_cast<uint8_t>(command), TNFS_RESULT_FILE_NOT_FOUND, zeros(2));
        bool is_known = std::find(known.begin(), known.end(), command) != known.end();
        CHECK((frame(r).status == tnfsFrameStatus::COMPLETE) == is_known);
        CHECK((frame(r).status == tnfsFrameStatus::MALFORMED) == !is_known);
    }
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

TEST_CASE("a full assembler holds a complete or a malformed reply, never a partial one")
{
    // So a reader that appends at most space() bytes can never stall on a full buffer.
    std::mt19937 rng(0x5EED);
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<Bytes> session = sample_session();
    for (int i = 0; i < 20000; i++)
    {
        Bytes b(tnfsReplyAssembler::CAPACITY);
        for (auto &x : b)
            x = static_cast<uint8_t>(byte(rng));
        const Bytes &r = session[i % session.size()];
        std::copy(r.begin(), r.begin() + std::min(r.size(), b.size()), b.begin());
        if (i % 3 == 0)
            b[5] = static_cast<uint8_t>(i); // READ counts and READDIRX entry counts vary
        REQUIRE(frame(b).status != tnfsFrameStatus::NEED_MORE);
    }
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

// --- Replies taken for a pending request (TCP) -------------------------------------------

namespace
{
constexpr uint16_t SESSION = 0xBEEF;

tnfsReplyContext request(uint8_t command, uint8_t seq = 1, uint16_t session = SESSION)
{ return {session, seq, command}; }

Bytes with_session(Bytes r, uint16_t session)
{
    r[0] = static_cast<uint8_t>(session);
    r[1] = static_cast<uint8_t>(session >> 8);
    return r;
}

Bytes cat(Bytes a, const Bytes &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Bytes strings(const std::string &uid, const std::string &gid)
{
    Bytes b(uid.begin(), uid.end());
    b.push_back('\0');
    b.insert(b.end(), gid.begin(), gid.end());
    b.push_back('\0');
    return b;
}

// paumaed's STAT: TNFSD's 22-byte block, then the uid and gid strings.
Bytes paumaed_stat(uint8_t seq, const std::string &uid = "fntest", const std::string &gid = "users")
{ return cat(reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, zeros(22), seq), strings(uid, gid)); }

struct Taken
{
    tnfsFrameStatus status;
    tnfsReplyKind kind;
    Bytes reply;
};

Taken take(tnfsReplyAssembler &a, const tnfsReplyContext &c)
{
    uint8_t out[TNFS_MAX_REPLY_SIZE];
    tnfsReplyKind kind = tnfsReplyKind::SESSIONLESS;
    tnfsFrame f = a.take(out, c, kind);
    Taken t{f.status, kind, {}};
    if (f.status == tnfsFrameStatus::COMPLETE)
        t.reply.assign(out, out + f.length);
    return t;
}

// The client's view: request i expects answers[i]. The stream is fed in `chunks`, and the
// next request goes out only after the previous answer was taken, as over a real link.
// Returns the answers taken, in order; stops at the first MALFORMED.
struct Exchange
{
    tnfsReplyContext context;
    Bytes answer; // what the client must get for it
};

std::vector<Bytes> converse(const std::vector<Exchange> &exchanges, const Bytes &stream,
                            const std::vector<int> &chunks, int *malformed = nullptr)
{
    tnfsReplyAssembler a;
    std::vector<Bytes> got;
    size_t pos = 0, c = 0;
    for (const Exchange &e : exchanges)
    {
        for (;;)
        {
            Taken t = take(a, e.context);
            if (t.status == tnfsFrameStatus::MALFORMED)
            {
                if (malformed)
                    (*malformed)++;
                return got;
            }
            if (t.status == tnfsFrameStatus::COMPLETE)
            {
                if (t.kind == tnfsReplyKind::ANSWER)
                {
                    got.push_back(t.reply);
                    break;
                }
                continue; // EARLIER and SESSIONLESS replies are skipped by the transport
            }
            if (pos >= stream.size())
                return got; // waiting for bytes that will not come
            int want = std::min<int>(chunks[c++ % chunks.size()], stream.size() - pos);
            pos += a.append(stream.data() + pos, want);
        }
    }
    return got;
}

std::vector<Bytes> answers(const std::vector<Exchange> &exchanges)
{
    std::vector<Bytes> r;
    for (const auto &e : exchanges)
        r.push_back(e.answer);
    return r;
}

// Every chunk size and every two-way split must give the same answers.
void check_every_split(const std::vector<Exchange> &exchanges, const Bytes &stream)
{
    for (int chunk = 1; chunk <= static_cast<int>(stream.size()); chunk++)
    {
        CAPTURE(chunk);
        REQUIRE(converse(exchanges, stream, {chunk}) == answers(exchanges));
    }
    for (int split = 1; split < static_cast<int>(stream.size()); split++)
    {
        CAPTURE(split);
        REQUIRE(converse(exchanges, stream, {split, static_cast<int>(stream.size())}) ==
                answers(exchanges));
    }
}

Bytes stat_block(uint8_t seq) { return reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, zeros(22), seq); }
} // namespace

TEST_CASE("context: TNFSD's replies are answers, whole or split at any point")
{
    std::vector<Exchange> ex;
    Bytes stream;
    uint8_t seq = 0;
    for (const Bytes &r : sample_session())
    {
        Bytes answer = r;
        answer[2] = seq;
        ex.push_back({request(r[3], seq), answer});
        stream = cat(stream, answer);
        seq++;
    }
    ex[0].context.session = 0; // MOUNT: no session yet
    check_every_split(ex, stream);
}

TEST_CASE("context: paumaed's STAT strings, any length, in one block or split anywhere")
{
    for (auto names : std::vector<std::pair<std::string, std::string>>{
             {"fntest", "users"}, {"", ""}, {"a", ""}, {"", "g"}, {std::string(250, 'u'), std::string(250, 'g')}})
    {
        CAPTURE(names.first.size());
        CAPTURE(names.second.size());
        Bytes stat = paumaed_stat(5, names.first, names.second);
        Bytes open = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x01}, 6);
        std::vector<Exchange> ex = {{request(TNFS_CMD_STAT, 5), stat_block(5)},
                                    {request(TNFS_CMD_OPEN, 6), open}};
        check_every_split(ex, cat(stat, open));
    }
}

TEST_CASE("context: the STAT strings arrive after the STAT was taken")
{
    tnfsReplyAssembler a;
    Bytes stat = paumaed_stat(5);
    a.append(stat.data(), 27);
    Taken t = take(a, request(TNFS_CMD_STAT, 5));
    REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
    CHECK(t.reply == stat_block(5));
    // Nothing more yet: the OPEN request waits, and holds no cut-off answer.
    CHECK(take(a, request(TNFS_CMD_OPEN, 6)).status == tnfsFrameStatus::NEED_MORE);
    a.append(stat.data() + 27, 4);
    CHECK(take(a, request(TNFS_CMD_OPEN, 6)).status == tnfsFrameStatus::NEED_MORE);
    CHECK_FALSE(a.may_hold_cut_answer(request(TNFS_CMD_OPEN, 6)));
    a.append(stat.data() + 31, static_cast<int>(stat.size()) - 31);
    CHECK(take(a, request(TNFS_CMD_OPEN, 6)).status == tnfsFrameStatus::NEED_MORE);
    CHECK_FALSE(a.may_hold_cut_answer(request(TNFS_CMD_OPEN, 6)));
    Bytes open = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x01}, 6);
    a.append(open.data(), 3);
    CHECK(take(a, request(TNFS_CMD_OPEN, 6)).status == tnfsFrameStatus::NEED_MORE);
    CHECK(a.may_hold_cut_answer(request(TNFS_CMD_OPEN, 6))); // the OPEN answer has begun
    a.append(open.data() + 3, 3);
    t = take(a, request(TNFS_CMD_OPEN, 6));
    REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
    CHECK(t.kind == tnfsReplyKind::ANSWER);
    CHECK(t.reply == open);
    CHECK(a.buffered() == 0);
}

TEST_CASE("context: READ and WRITE failures with and without paumaed's zero count")
{
    struct Case
    {
        uint8_t command, status;
    };
    for (Case k : {Case{TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE}, Case{TNFS_CMD_READ, TNFS_RESULT_BAD_FILENUM},
                   Case{TNFS_CMD_READ, TNFS_RESULT_INVALID_HANDLE}, Case{TNFS_CMD_WRITE, TNFS_RESULT_BAD_FILENUM},
                   Case{TNFS_CMD_WRITE, TNFS_RESULT_IO_ERROR}})
        for (bool count : {false, true})
        {
            CAPTURE(int(k.command));
            CAPTURE(int(k.status));
            CAPTURE(count);
            Bytes failed = reply(k.command, k.status, {}, 7);
            Bytes close = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 8);
            std::vector<Exchange> ex = {{request(k.command, 7), failed}, {request(TNFS_CMD_CLOSE, 8), close}};
            check_every_split(ex, cat(cat(failed, count ? Bytes{0, 0} : Bytes{}), close));
        }
}

TEST_CASE("context: a retry's duplicate and a late reply are earlier replies, not answers")
{
    Bytes stat = paumaed_stat(5);
    Bytes open = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x01}, 6);
    Bytes stream = cat(cat(stat, stat), open); // STAT answered twice, then OPEN
    std::vector<Exchange> ex = {{request(TNFS_CMD_STAT, 5), stat_block(5)}, {request(TNFS_CMD_OPEN, 6), open}};
    check_every_split(ex, stream);

    tnfsReplyAssembler a;
    a.append(stream.data(), static_cast<int>(stream.size()));
    REQUIRE(take(a, request(TNFS_CMD_STAT, 5)).kind == tnfsReplyKind::ANSWER);
    Taken dup = take(a, request(TNFS_CMD_OPEN, 6));
    REQUIRE(dup.status == tnfsFrameStatus::COMPLETE);
    CHECK(dup.kind == tnfsReplyKind::EARLIER);
    CHECK(dup.reply == stat_block(5));
    Taken t = take(a, request(TNFS_CMD_OPEN, 6));
    CHECK(t.kind == tnfsReplyKind::ANSWER);
    CHECK(t.reply == open);
}

TEST_CASE("context: a header that is not for the request is malformed, never an answer")
{
    Bytes good = reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, zeros(22), 5);
    struct Case
    {
        const char *what;
        Bytes bytes;
    };
    Bytes other_command = reply(TNFS_CMD_LSEEK, TNFS_RESULT_SUCCESS, zeros(4), 5);
    Bytes unknown_command = good;
    unknown_command[3] = 0x99;
    Bytes unknown_status = good;
    unknown_status[4] = 0x80;
    for (const Case &k : {Case{"same sequence, another command", other_command},
                          Case{"another session", with_session(good, 0xBEEE)},
                          Case{"session 0 with success", with_session(good, 0)},
                          Case{"unknown command", unknown_command},
                          Case{"unknown status", unknown_status},
                          Case{"text", {'f', 'n', 't', 'e', 's', 't', 0}}})
    {
        CAPTURE(k.what);
        tnfsReplyAssembler a;
        a.append(k.bytes.data(), static_cast<int>(k.bytes.size()));
        CHECK(take(a, request(TNFS_CMD_STAT, 5)).status == tnfsFrameStatus::MALFORMED);
        CHECK(a.buffered() == 0);
    }
}

TEST_CASE("context: an expired session answered with session 0 is still the answer")
{
    Bytes invalid = with_session(reply(TNFS_CMD_STAT, TNFS_RESULT_INVALID_HANDLE, {}, 5), 0);
    tnfsReplyAssembler a;
    a.append(invalid.data(), static_cast<int>(invalid.size()));
    Taken t = take(a, request(TNFS_CMD_STAT, 5));
    REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
    CHECK(t.kind == tnfsReplyKind::ANSWER);
}

TEST_CASE("context: TNFSD's sessionless replies to a split request are skipped")
{
    // From TNFSD 26.0915.1: EINVAL for the request's first segment, then INVALID with
    // session 0 for its second, read as a request of its own.
    Bytes einval = reply(TNFS_CMD_STAT, TNFS_RESULT_INVALID_ARGUMENT, {}, 1);
    Bytes stray = {0x00, 0x00, 0x61, 0x6d, 0xff};
    Bytes eperm = {0x00, 0x00, 0x62, 0x62, TNFS_RESULT_NOT_PERMITTED};
    Bytes open = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x01}, 2);
    std::vector<Exchange> ex = {{request(TNFS_CMD_STAT, 1), einval}, {request(TNFS_CMD_OPEN, 2), open}};
    check_every_split(ex, cat(cat(cat(einval, stray), eperm), open));
}

TEST_CASE("context: when both readings fit, the stream is malformed (documented limit)")
{
    // Session 0x0041 and uid "A": the uid string and a gid of "\x05\x24" read as a STAT
    // answer header (41 00 05 24 00). Followed by a real STAT answer, both readings fit.
    const uint16_t s41 = 0x0041;
    Bytes stat = with_session(stat_block(4), s41);
    Bytes tail = strings("A", std::string{0x05, 0x24});
    Bytes next = with_session(stat_block(5), s41);
    tnfsReplyAssembler a;
    Bytes stream = cat(cat(stat, tail), next);
    a.append(stream.data(), static_cast<int>(stream.size()));
    REQUIRE(take(a, request(TNFS_CMD_STAT, 4, s41)).kind == tnfsReplyKind::ANSWER);
    CHECK(take(a, request(TNFS_CMD_STAT, 5, s41)).status == tnfsFrameStatus::MALFORMED);
}

TEST_CASE("context: only a cut-off answer makes a timeout replace the connection")
{
    Bytes stat = paumaed_stat(5);
    tnfsReplyAssembler a;
    a.append(stat.data(), static_cast<int>(stat.size()));
    REQUIRE(take(a, request(TNFS_CMD_STAT, 5)).kind == tnfsReplyKind::ANSWER);
    CHECK_FALSE(a.may_hold_cut_answer(request(TNFS_CMD_OPEN, 6))); // only the strings

    tnfsReplyAssembler b;
    Bytes eof = cat(reply(TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE, {}, 7), {0, 0});
    b.append(eof.data(), static_cast<int>(eof.size()));
    REQUIRE(take(b, request(TNFS_CMD_READ, 7)).kind == tnfsReplyKind::ANSWER);
    CHECK_FALSE(b.may_hold_cut_answer(request(TNFS_CMD_CLOSE, 8))); // only the zero count

    tnfsReplyAssembler c;
    Bytes next = reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, zeros(22), 6);
    c.append(next.data(), 10);
    CHECK(c.may_hold_cut_answer(request(TNFS_CMD_STAT, 6)));
    CHECK(take(c, request(TNFS_CMD_STAT, 6)).status == tnfsFrameStatus::NEED_MORE);

    tnfsReplyAssembler d;
    d.append(next.data(), 2); // the start of our own header
    CHECK(d.may_hold_cut_answer(request(TNFS_CMD_STAT, 6)));
    CHECK_FALSE(d.may_hold_cut_answer(request(TNFS_CMD_STAT, 6, 0x1234)));
}

TEST_CASE("context: a full assembler with an unsettled field still takes a maximum reply")
{
    Bytes stat = paumaed_stat(5, std::string(250, 'u'), std::string(250, 'g'));
    Bytes big = readdirx_reply({std::string(255, 'n'), std::string(240, 'm')});
    big[2] = 6;
    REQUIRE(big.size() <= TNFS_MAX_REPLY_SIZE);
    std::vector<Exchange> ex = {{request(TNFS_CMD_STAT, 5), stat_block(5)},
                                {request(TNFS_CMD_READDIRX, 6), big}};
    REQUIRE(converse(ex, cat(stat, big), {tnfsReplyAssembler::CAPACITY}) == answers(ex));
}

TEST_CASE("context fuzz: an answer always matches the request, nothing overflows")
{
    std::mt19937 rng(0x2008);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<int> len(0, 600);
    std::vector<Bytes> session = sample_session();
    for (int i = 0; i < 20000; i++)
    {
        tnfsReplyContext c = request(static_cast<uint8_t>(session[i % session.size()][3]),
                                     static_cast<uint8_t>(byte(rng)), i % 5 ? SESSION : 0);
        Bytes b(len(rng));
        for (auto &x : b)
            x = static_cast<uint8_t>(byte(rng));
        if (i % 2) // start with something that looks right: a reply, or paumaed's strings
        {
            Bytes head = i % 4 == 1 ? paumaed_stat(c.sequence) : session[i % session.size()];
            head[2] = c.sequence;
            std::copy(head.begin(), head.begin() + std::min(head.size(), b.size()), b.begin());
        }
        tnfsReplyAssembler a;
        a.append(b.data(), static_cast<int>(b.size()));
        for (int n = 0; n < 8; n++)
        {
            int before = a.buffered();
            Taken t = take(a, c);
            if (t.status != tnfsFrameStatus::COMPLETE)
                break;
            REQUIRE(t.reply.size() >= 5);
            REQUIRE(static_cast<int>(t.reply.size()) <= TNFS_MAX_REPLY_SIZE);
            REQUIRE(a.buffered() <= before - static_cast<int>(t.reply.size()));
            uint16_t sid = t.reply[0] | t.reply[1] << 8;
            if (t.kind == tnfsReplyKind::ANSWER)
            {
                REQUIRE(t.reply[2] == c.sequence);
                REQUIRE(t.reply[3] == c.command);
                bool sessionless = sid == 0 && (t.reply[4] == TNFS_RESULT_INVALID_HANDLE ||
                                                t.reply[4] == TNFS_RESULT_NOT_PERMITTED);
                REQUIRE((c.command == TNFS_CMD_MOUNT || sid == c.session || sessionless));
            }
            else if (t.kind == tnfsReplyKind::EARLIER)
            {
                REQUIRE(sid == c.session);
                REQUIRE(t.reply[2] != c.sequence);
            }
            else
            {
                REQUIRE(sid == 0);
                REQUIRE(t.reply.size() == 5);
            }
        }
    }
}

TEST_CASE("context: an earlier-looking reading does not win while an answer may be arriving")
{
    // Session 0x0041: uid "A" and gid "\x03)" read as this session's earlier OPEN reply
    // (41 00 03 29 00), whose handle byte would be the first byte of the real answer.
    const uint16_t s41 = 0x0041;
    Bytes stat = with_session(stat_block(4), s41);
    Bytes tail = strings("A", std::string{0x03, 0x29});
    Bytes open = with_session(reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x01}, 5), s41);
    std::vector<Exchange> ex = {{request(TNFS_CMD_STAT, 4, s41), stat},
                                {request(TNFS_CMD_OPEN, 5, s41), open}};
    Bytes stream = cat(cat(stat, tail), open);
    for (int split = 1; split < static_cast<int>(stream.size()); split++)
    {
        CAPTURE(split);
        REQUIRE(converse(ex, stream, {split, static_cast<int>(stream.size())}) == answers(ex));
    }
}

// --- R1: the optional field shaped like the next answer ----------------------------------

namespace
{
constexpr uint16_t S41 = 0x0041; // "A\0" reads as this session id

Bytes slice(const Bytes &b, size_t from, size_t to = SIZE_MAX)
{ return Bytes(b.begin() + from, b.begin() + std::min(to, b.size())); }

// STAT `seq` from session 0x0041 with uid "A" and gid `gid`, then the request seq + 1.
struct R1Stream
{
    tnfsReplyAssembler a;
    Bytes tail;

    R1Stream(uint8_t seq, const std::string &gid, bool tail_before_next_request)
    {
        Bytes block = with_session(stat_block(seq), S41);
        tail = strings("A", gid);
        a.request_sent(seq, 0);
        Bytes first = tail_before_next_request ? cat(block, tail) : block;
        a.append(first.data(), static_cast<int>(first.size()));
        Taken t = take(a, request(TNFS_CMD_STAT, seq, S41));
        REQUIRE(t.kind == tnfsReplyKind::ANSWER);
        REQUIRE(t.reply == block);
    }
    void send(uint8_t seq, int unread = 0) { a.request_sent(seq, unread); }
    void arrive(const Bytes &b) { a.append(b.data(), static_cast<int>(b.size())); }
};
} // namespace

TEST_CASE("R1: strings received before the next request went out are not its answer")
{
    struct Case
    {
        const char *what;
        uint8_t command;
        std::string gid_tail; // after the sequence byte
        Bytes answer_data;
        uint8_t status;
    };
    for (const Case &k : {Case{"CLOSE success", TNFS_CMD_CLOSE, "#", {}, TNFS_RESULT_BAD_FILENUM},
                          Case{"OPEN handle", TNFS_CMD_OPEN, ")", {0x03}, TNFS_RESULT_SUCCESS},
                          Case{"READ end of file", TNFS_CMD_READ, "!!", {0x02, 0x00, 0x11, 0x22},
                               TNFS_RESULT_SUCCESS}})
        for (int first_part : {0, 1, 3})
        {
            CAPTURE(k.what);
            CAPTURE(first_part);
            R1Stream s('x' - 1, std::string(1, 'x') + k.gid_tail, true);
            s.send('x');
            Bytes answer = with_session(reply(k.command, k.status, k.answer_data, 'x'), S41);
            tnfsReplyContext next = request(k.command, 'x', S41);
            // The answer comes a round trip later, whole or in two parts.
            CHECK(take(s.a, next).status == tnfsFrameStatus::NEED_MORE);
            CHECK_FALSE(s.a.may_hold_cut_answer(next));
            if (first_part)
            {
                s.arrive(slice(answer, 0, first_part));
                CHECK(take(s.a, next).status == tnfsFrameStatus::NEED_MORE);
                s.arrive(slice(answer, first_part));
            }
            else
                s.arrive(answer);
            Taken t = take(s.a, next);
            REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
            CHECK(t.kind == tnfsReplyKind::ANSWER);
            CHECK(t.reply == answer);
            CHECK(s.a.buffered() == 0);
        }
}

TEST_CASE("R1: strings already received but not yet read when the request went out")
{
    // The STAT was framed from its first 27 bytes; the strings sat in the socket, unread.
    R1Stream s('x' - 1, "x#", false);
    s.send('x', static_cast<int>(s.tail.size()));
    s.arrive(s.tail);
    tnfsReplyContext next = request(TNFS_CMD_CLOSE, 'x', S41);
    CHECK(take(s.a, next).status == tnfsFrameStatus::NEED_MORE);
    Bytes answer = with_session(reply(TNFS_CMD_CLOSE, TNFS_RESULT_BAD_FILENUM, {}, 'x'), S41);
    s.arrive(answer);
    Taken t = take(s.a, next);
    CHECK(t.kind == tnfsReplyKind::ANSWER);
    CHECK(t.reply == answer);
}

TEST_CASE("R1: a retry keeps the mark, a new connection clears it")
{
    Bytes open = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x03}, 6);
    tnfsReplyAssembler a;
    a.request_sent(6, 0);
    a.append(open.data(), static_cast<int>(open.size())); // arrives before the timeout
    a.request_sent(6, 0);                                  // the retry
    Taken t = take(a, request(TNFS_CMD_OPEN, 6));
    CHECK(t.kind == tnfsReplyKind::ANSWER);

    tnfsReplyAssembler b;
    b.append(open.data(), static_cast<int>(open.size()));
    b.request_sent(6, 0); // the answer-shaped bytes predate the request: an earlier reply
    t = take(b, request(TNFS_CMD_OPEN, 6));
    REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
    CHECK(t.kind == tnfsReplyKind::EARLIER);
    b.reset();
    b.append(open.data(), static_cast<int>(open.size()));
    t = take(b, request(TNFS_CMD_OPEN, 6)); // no mark on the new stream yet
    CHECK(t.kind == tnfsReplyKind::ANSWER);
}

TEST_CASE("STAT strings received only after the next request went out (documented limit)")
{
    // The same bytes, in the same order relative to the request, from two servers:
    //  P: STAT strings "A\0x#\0" whose segment the network delayed past the CLOSE request;
    //  T: no strings; session 0x0041 answers that CLOSE with 41 00 78 23 00 (success).
    // T sends nothing more, so the client must take those bytes as the answer at once, and
    // reading the same bytes it cannot tell P apart. This is the residual risk of R1.
    tnfsReplyAssembler a;
    Bytes block = with_session(stat_block('x' - 1), S41);
    a.request_sent('x' - 1, 0);
    a.append(block.data(), static_cast<int>(block.size()));
    REQUIRE(take(a, request(TNFS_CMD_STAT, 'x' - 1, S41)).kind == tnfsReplyKind::ANSWER);
    a.request_sent('x', 0);
    Bytes later = strings("A", "x#"); // P: the strings; T: its whole answer
    a.append(later.data(), static_cast<int>(later.size()));
    Taken t = take(a, request(TNFS_CMD_CLOSE, 'x', S41));
    REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
    CHECK(t.kind == tnfsReplyKind::ANSWER); // right for T, wrong for P
    CHECK(t.reply == later);
}
