#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "tnfslib_framing.h"

// Reply formats (FMT_FIXED, FMT_EXTENDED, FMT_UNKNOWN) and the fence that settles an FMT_UNKNOWN field.

using Bytes = std::vector<uint8_t>;

namespace
{
constexpr uint16_t SESSION = 0xBEEF;
constexpr uint16_t S41 = 0x0041;   // paumaed-like: "A\0" reads as this session id
constexpr uint16_t S4241 = 0x4241; // two non-zero bytes: "AB" reads as this session id

const tnfsReplyFormat FMT_FIXED = tnfsReplyFormat::fixed();
const tnfsReplyFormat FMT_EXTENDED = tnfsReplyFormat::extended();
const tnfsReplyFormat FMT_UNKNOWN{};

Bytes reply(uint8_t command, uint8_t status, const Bytes &data = {}, uint8_t seq = 1,
            uint16_t session = SESSION)
{
    Bytes r = {static_cast<uint8_t>(session), static_cast<uint8_t>(session >> 8), seq, command, status};
    r.insert(r.end(), data.begin(), data.end());
    return r;
}

Bytes cat(Bytes a, const Bytes &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Bytes slice(const Bytes &b, size_t from, size_t to = SIZE_MAX)
{ return Bytes(b.begin() + std::min(from, b.size()), b.begin() + std::min(to, b.size())); }

Bytes strings(const std::string &uid, const std::string &gid)
{
    Bytes b(uid.begin(), uid.end());
    b.push_back('\0');
    b.insert(b.end(), gid.begin(), gid.end());
    b.push_back('\0');
    return b;
}

Bytes stat_block(uint8_t seq, uint16_t session = SESSION)
{ return reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, Bytes(22, 0x11), seq, session); }

const Bytes COUNT = {0x00, 0x00}; // paumaed's count after a failed READ or WRITE

tnfsReplyContext request(uint8_t command, uint8_t seq, const tnfsReplyFormat &format,
                         uint16_t session = SESSION)
{ return {session, seq, command, format}; }

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

void arrive(tnfsReplyAssembler &a, const Bytes &b) { a.append(b.data(), static_cast<int>(b.size())); }

// The client's side of one connection, one request at a time as the transport does it: the
// request is marked as sent, then replies are taken until its answer; an FMT_UNKNOWN field left
// by the answer is settled with a fence before the next request (IDENTIFY).
struct Client
{
    tnfsReplyAssembler a;
    tnfsReplyFormat format;
    uint8_t seq = 1;

    explicit Client(const tnfsReplyFormat &f) : format(f) {}

    tnfsReplyContext send(uint8_t command, uint16_t session = SESSION)
    {
        a.request_sent(seq, 0);
        return request(command, seq++, format, session);
    }
    tnfsReplyContext send_fence()
    {
        a.request_sent(seq, 0);
        return {0, seq++, TNFS_CMD_CLOSE, {}};
    }
};

Bytes fence_answer(uint8_t seq) { return {0x00, 0x00, seq, TNFS_CMD_CLOSE, TNFS_RESULT_INVALID_HANDLE}; }

void set_use(tnfsReplyFormat &f, tnfsOptionalField field, tnfsFieldUse use)
{
    if (field == tnfsOptionalField::STAT_STRINGS)
        f.stat_strings = use;
    else if (field == tnfsOptionalField::READ_COUNT)
        f.read_count = use;
    else
        f.write_count = use;
}
} // namespace

// --- Known formats: every reply framed exactly, nothing deferred ------------------------

TEST_CASE("FIXED: TNFSD's replies, each followed at once by the next answer, split anywhere")
{
    // STAT block, READ EOF, WRITE error: all end at their status or block; nothing deferred.
    std::vector<std::pair<tnfsReplyContext, Bytes>> ex = {
        {request(TNFS_CMD_STAT, 1, FMT_FIXED), stat_block(1)},
        {request(TNFS_CMD_READ, 2, FMT_FIXED), reply(TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE, {}, 2)},
        {request(TNFS_CMD_WRITE, 3, FMT_FIXED), reply(TNFS_CMD_WRITE, TNFS_RESULT_BAD_FILENUM, {}, 3)},
        {request(TNFS_CMD_CLOSE, 4, FMT_FIXED), reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 4)},
    };
    Bytes stream;
    for (auto &e : ex)
        stream = cat(stream, e.second);
    for (size_t split = 0; split <= stream.size(); split++)
    {
        CAPTURE(split);
        tnfsReplyAssembler a;
        size_t fed = 0;
        auto feed_to = [&](size_t n) { arrive(a, slice(stream, fed, n)), fed = std::max(fed, n); };
        feed_to(split);
        for (auto &e : ex)
        {
            Taken t = take(a, e.first);
            if (t.status == tnfsFrameStatus::NEED_MORE)
            {
                feed_to(stream.size());
                t = take(a, e.first);
            }
            REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
            CHECK(t.kind == tnfsReplyKind::ANSWER);
            CHECK(t.reply == e.second);
            CHECK(a.unsettled() == tnfsOptionalField::NONE);
        }
        CHECK(a.buffered() == 0);
    }
}

TEST_CASE("EXTENDED: paumaed's optional fields are part of their replies, split anywhere")
{
    std::vector<std::pair<tnfsReplyContext, Bytes>> ex = {
        {request(TNFS_CMD_STAT, 1, FMT_EXTENDED), cat(stat_block(1), strings("fntest", "users"))},
        {request(TNFS_CMD_STAT, 2, FMT_EXTENDED), cat(stat_block(2), strings("", ""))},
        {request(TNFS_CMD_READ, 3, FMT_EXTENDED), cat(reply(TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE, {}, 3), COUNT)},
        {request(TNFS_CMD_READ, 4, FMT_EXTENDED), cat(reply(TNFS_CMD_READ, TNFS_RESULT_INVALID_HANDLE, {}, 4), COUNT)},
        {request(TNFS_CMD_WRITE, 5, FMT_EXTENDED), cat(reply(TNFS_CMD_WRITE, TNFS_RESULT_BAD_FILENUM, {}, 5), COUNT)},
        // paumaed leaves the count out of a WRITE's INVALID
        {request(TNFS_CMD_WRITE, 6, FMT_EXTENDED), reply(TNFS_CMD_WRITE, TNFS_RESULT_INVALID_HANDLE, {}, 6)},
        {request(TNFS_CMD_CLOSE, 7, FMT_EXTENDED), reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 7)},
    };
    Bytes stream;
    for (auto &e : ex)
        stream = cat(stream, e.second);
    for (size_t split = 0; split <= stream.size(); split++)
    {
        CAPTURE(split);
        tnfsReplyAssembler a;
        size_t fed = 0;
        auto feed_to = [&](size_t n) { arrive(a, slice(stream, fed, n)), fed = std::max(fed, n); };
        feed_to(split);
        for (auto &e : ex)
        {
            Taken t = take(a, e.first);
            if (t.status == tnfsFrameStatus::NEED_MORE)
            {
                feed_to(stream.size());
                t = take(a, e.first);
            }
            REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
            CHECK(t.kind == tnfsReplyKind::ANSWER);
            CHECK(t.reply == e.second);
            CHECK(a.unsettled() == tnfsOptionalField::NONE);
        }
        CHECK(a.buffered() == 0);
    }
}

TEST_CASE("EXTENDED: a reply is not complete until its field is, and a timeout replaces the stream")
{
    tnfsReplyAssembler a;
    tnfsReplyContext stat = request(TNFS_CMD_STAT, 1, FMT_EXTENDED);
    arrive(a, stat_block(1));
    CHECK(take(a, stat).status == tnfsFrameStatus::NEED_MORE); // the strings are still to come
    CHECK(a.may_hold_cut_answer(stat));                         // so a retry's reply must not complete it
    arrive(a, {'u', 0x00, 'g'});
    CHECK(take(a, stat).status == tnfsFrameStatus::NEED_MORE);
    arrive(a, {0x00});
    CHECK(take(a, stat).status == tnfsFrameStatus::COMPLETE);

    tnfsReplyAssembler b;
    tnfsReplyContext read = request(TNFS_CMD_READ, 2, FMT_EXTENDED);
    arrive(b, reply(TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE, {}, 2));
    CHECK(take(b, read).status == tnfsFrameStatus::NEED_MORE);
    CHECK(b.may_hold_cut_answer(read));
    arrive(b, {0x00, 0x01}); // not a zero count: not paumaed's format
    CHECK(take(b, read).status == tnfsFrameStatus::MALFORMED);
}

// --- A field delayed past the next request ----------------------------------------------

namespace
{
// A STAT whose strings are shaped like the answer to the request that follows it, and that
// the network delays until after that request went out.
// What paumaed puts on the wire for an answer: a failed READ or WRITE adds its count.
Bytes on_wire(const Bytes &answer)
{
    bool failed_io = (answer[3] == TNFS_CMD_READ || answer[3] == TNFS_CMD_WRITE) &&
                     answer[4] != TNFS_RESULT_SUCCESS;
    return failed_io ? cat(answer, COUNT) : answer;
}

struct R1Prime
{
    const char *what;
    uint8_t command;
    uint16_t session;
    std::string uid, gid; // seq 'x' is the next request's
    Bytes answer_data;    // the server's real answer to it
    uint8_t answer_status;
};

const std::vector<R1Prime> R1_PRIME = {
    {"CLOSE: success instead of EBADF", TNFS_CMD_CLOSE, S41, "A", "x#", {}, TNFS_RESULT_BAD_FILENUM},
    {"OPEN: handle 0x41 instead of 3", TNFS_CMD_OPEN, S41, "A", "x)", {0x03}, TNFS_RESULT_SUCCESS},
    {"READ: EOF instead of data", TNFS_CMD_READ, S41, "A", "x!!", {0x02, 0x00, 0x11, 0x22}, TNFS_RESULT_SUCCESS},
    {"WRITE: success instead of ENOSPC", TNFS_CMD_WRITE, S4241, "ABx\"", "CD", {}, TNFS_RESULT_NO_SPACE_ON_DEVICE},
};
} // namespace

TEST_CASE("Delayed field, UNKNOWN left to the next reply (INFER) takes the strings for the answer")
{
    for (const R1Prime &k : R1_PRIME)
    {
        CAPTURE(k.what);
        Client c(FMT_UNKNOWN);
        c.seq = 'x' - 1;
        tnfsReplyContext stat = c.send(TNFS_CMD_STAT, k.session);
        arrive(c.a, stat_block(stat.sequence, k.session));
        REQUIRE(take(c.a, stat).kind == tnfsReplyKind::ANSWER);
        CHECK(c.a.unsettled() == tnfsOptionalField::STAT_STRINGS);
        tnfsReplyContext next = c.send(k.command, k.session); // INFER: no fence
        Bytes tail = strings(k.uid, k.gid);
        Bytes answer = reply(k.command, k.answer_status, k.answer_data, next.sequence, k.session);
        arrive(c.a, tail);                  // the delayed segment
        arrive(c.a, slice(answer, 0, 1));   // a round trip later, the answer starts
        Taken t = take(c.a, next);
        REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
        CHECK(t.kind == tnfsReplyKind::ANSWER);
        CHECK(t.reply != answer); // the wrong answer, taken silently
    }
}

TEST_CASE("Delayed field handled: EXTENDED frames the strings with the STAT, before any request goes out")
{
    for (const R1Prime &k : R1_PRIME)
        for (size_t at = 0; at <= k.uid.size() + k.gid.size() + 2; at++)
        {
            CAPTURE(k.what);
            CAPTURE(at);
            Client c(FMT_EXTENDED);
            c.seq = 'x' - 1;
            tnfsReplyContext stat = c.send(TNFS_CMD_STAT, k.session);
            Bytes tail = strings(k.uid, k.gid);
            arrive(c.a, cat(stat_block(stat.sequence, k.session), slice(tail, 0, at)));
            Taken t = take(c.a, stat);
            if (at < tail.size())
            {
                // The STAT is not done: the client cannot have sent the next request yet.
                CHECK(t.status == tnfsFrameStatus::NEED_MORE);
                arrive(c.a, slice(tail, at));
                t = take(c.a, stat);
            }
            REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
            CHECK(t.reply == cat(stat_block(stat.sequence, k.session), tail));
            tnfsReplyContext next = c.send(k.command, k.session);
            Bytes answer = on_wire(reply(k.command, k.answer_status, k.answer_data, next.sequence, k.session));
            arrive(c.a, slice(answer, 0, 1));
            CHECK(take(c.a, next).status == tnfsFrameStatus::NEED_MORE);
            arrive(c.a, slice(answer, 1));
            t = take(c.a, next);
            REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
            CHECK(t.reply == answer);
            CHECK(c.a.buffered() == 0);
        }
}

TEST_CASE("Delayed field handled: IDENTIFY settles the strings with a fence before the next request")
{
    // The tail may be shaped like the answer to the request the client really sends next
    // (after the fence), or like the fence's own answer slot; it arrives at any point.
    for (const R1Prime &k : R1_PRIME)
        for (int shift : {0, 1})
            for (size_t at = 0; at <= 40; at++)
            {
                CAPTURE(k.what);
                CAPTURE(shift);
                CAPTURE(at);
                Client c(FMT_UNKNOWN);
                c.seq = static_cast<uint8_t>('x' - 1 - shift);
                tnfsReplyContext stat = c.send(TNFS_CMD_STAT, k.session);
                Bytes tail = strings(k.uid, k.gid);
                Bytes block = stat_block(stat.sequence, k.session);
                arrive(c.a, block);
                REQUIRE(take(c.a, stat).reply == block);
                REQUIRE(c.a.unsettled() == tnfsOptionalField::STAT_STRINGS);
                tnfsReplyContext fence = c.send_fence();
                Bytes rest = cat(tail, fence_answer(fence.sequence)); // TCP keeps the order
                arrive(c.a, slice(rest, 0, at));
                tnfsFenceVerdict v = c.a.settle(fence);
                if (v == tnfsFenceVerdict::NEED_MORE)
                {
                    arrive(c.a, slice(rest, at));
                    v = c.a.settle(fence);
                }
                REQUIRE(v == tnfsFenceVerdict::PRESENT);
                CHECK(c.a.buffered() == 0);
                c.format.stat_strings = tnfsFieldUse::PRESENT;
                tnfsReplyContext next = c.send(k.command, k.session);
                Bytes answer = reply(k.command, k.answer_status, k.answer_data, next.sequence, k.session);
                arrive(c.a, on_wire(answer)); // its own count, if any, is the next field to settle
                Taken t = take(c.a, next);
                REQUIRE(t.status == tnfsFrameStatus::COMPLETE);
                CHECK(t.reply == answer);
            }
}

// --- The fence ---------------------------------------------------------------------------

TEST_CASE("fence: the verdict is right for any field content, split anywhere, in both worlds")
{
    // Random uid/gid strings (any non-zero bytes, empty included), both READ and WRITE
    // counts; the answer before the fence is from a session with zero or non-zero bytes.
    std::mt19937 rng(2024);
    auto random_string = [&rng](int max)
    {
        std::string s(rng() % (max + 1), 'a');
        for (char &ch : s)
            ch = static_cast<char>(1 + rng() % 255);
        return s;
    };
    const tnfsOptionalField fields[] = {tnfsOptionalField::STAT_STRINGS, tnfsOptionalField::READ_COUNT,
                                        tnfsOptionalField::WRITE_COUNT};
    int verdicts = 0;
    for (int round = 0; round < 1500; round++)
        for (tnfsOptionalField field : fields)
            for (bool present : {false, true})
            {
                uint16_t session = round % 3 == 0 ? S41 : round % 3 == 1 ? S4241 : SESSION;
                uint8_t seq = static_cast<uint8_t>(rng());
                Bytes answer, value;
                uint8_t command = TNFS_CMD_STAT;
                if (field == tnfsOptionalField::STAT_STRINGS)
                {
                    answer = stat_block(seq, session);
                    std::string uid = random_string(round % 50 == 0 ? 400 : 12);
                    value = strings(uid, random_string(12));
                }
                else
                {
                    command = field == tnfsOptionalField::READ_COUNT ? TNFS_CMD_READ : TNFS_CMD_WRITE;
                    answer = reply(command, TNFS_RESULT_END_OF_FILE + (command == TNFS_CMD_WRITE), {}, seq, session);
                    if (command == TNFS_CMD_WRITE)
                        answer[4] = TNFS_RESULT_BAD_FILENUM;
                    value = COUNT;
                }
                CAPTURE(round);
                CAPTURE(present);
                tnfsReplyAssembler a;
                a.request_sent(seq, 0);
                arrive(a, answer);
                Taken t = take(a, request(command, seq, FMT_UNKNOWN, session));
                REQUIRE(t.kind == tnfsReplyKind::ANSWER);
                REQUIRE(a.unsettled() == field);
                uint8_t fseq = static_cast<uint8_t>(seq + 1);
                Bytes rest = cat(present ? value : Bytes{}, fence_answer(fseq));
                // The field may already be in when the fence goes out (marked as unread).
                size_t before = rng() % (present ? value.size() + 1 : 1);
                arrive(a, slice(rest, 0, before));
                a.request_sent(fseq, 0);
                tnfsReplyContext fence{0, fseq, TNFS_CMD_CLOSE, {}};
                tnfsFenceVerdict v = tnfsFenceVerdict::NEED_MORE;
                for (size_t at = before; at < rest.size() && v == tnfsFenceVerdict::NEED_MORE;)
                {
                    size_t n = 1 + rng() % 7;
                    arrive(a, slice(rest, at, at + n));
                    at += n;
                    v = a.settle(fence);
                }
                REQUIRE(v == (present ? tnfsFenceVerdict::PRESENT : tnfsFenceVerdict::ABSENT));
                CHECK(a.buffered() == 0);
                CHECK(a.unsettled() == tnfsOptionalField::NONE);
                CHECK_FALSE(a.replies_pending());
                verdicts++;
            }
    CHECK(verdicts == 1500 * 3 * 2);
}

TEST_CASE("fence: the verdict is never reached before the bytes decide it")
{
    // Shortest cases: empty strings ("\0\0") against no strings, where both worlds share
    // the first three bytes (00 00 then 00 or the fence's sequence when it is 0).
    for (uint8_t fseq : {uint8_t(0), uint8_t(1), uint8_t(0x23)})
        for (bool present : {false, true})
        {
            CAPTURE(fseq);
            CAPTURE(present);
            tnfsReplyAssembler a;
            a.request_sent(9, 0);
            arrive(a, stat_block(9));
            REQUIRE(take(a, request(TNFS_CMD_STAT, 9, FMT_UNKNOWN)).kind == tnfsReplyKind::ANSWER);
            a.request_sent(fseq, 0);
            Bytes rest = cat(present ? strings("", "") : Bytes{}, fence_answer(fseq));
            tnfsReplyContext fence{0, fseq, TNFS_CMD_CLOSE, {}};
            tnfsFenceVerdict v = tnfsFenceVerdict::NEED_MORE;
            size_t at = 0;
            while (v == tnfsFenceVerdict::NEED_MORE && at < rest.size())
            {
                arrive(a, {rest[at++]});
                v = a.settle(fence);
            }
            CHECK(v == (present ? tnfsFenceVerdict::PRESENT : tnfsFenceVerdict::ABSENT));
            CHECK(at == rest.size()); // decided only by the last byte of the fence's answer
        }
}

TEST_CASE("fence: anything but its answer fails, and nothing is guessed")
{
    struct Case
    {
        const char *what;
        Bytes after; // what follows the STAT answer (fence sequence 7)
    };
    for (const Case &k : {
             Case{"answered for our session (a server that ignores sessions)",
                  reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 7)},
             Case{"another sequence", {0x00, 0x00, 8, TNFS_CMD_CLOSE, 0xFF}},
             Case{"another command", {0x00, 0x00, 7, TNFS_CMD_OPEN, 0xFF}},
             Case{"EAGAIN (would carry data)", {0x00, 0x00, 7, TNFS_CMD_CLOSE, TNFS_RESULT_TRY_AGAIN, 1, 0}},
             Case{"unknown status", {0x00, 0x00, 7, TNFS_CMD_CLOSE, 0x77}},
             Case{"a stray reply of TNFSD before it", cat({0x00, 0x00, 0x61, 0x6d, 0xff}, fence_answer(7))},
             Case{"a duplicate STAT before it", cat(stat_block(6), fence_answer(7))},
         })
    {
        CAPTURE(k.what);
        tnfsReplyAssembler a;
        a.request_sent(6, 0);
        arrive(a, stat_block(6));
        REQUIRE(take(a, request(TNFS_CMD_STAT, 6, FMT_UNKNOWN)).kind == tnfsReplyKind::ANSWER);
        a.request_sent(7, 0);
        arrive(a, k.after);
        tnfsReplyContext fence{0, 7, TNFS_CMD_CLOSE, {}};
        // FAILED, or NEED_MORE until the transport's timeout: never a verdict.
        tnfsFenceVerdict v = a.settle(fence);
        CHECK(v != tnfsFenceVerdict::ABSENT);
        CHECK(v != tnfsFenceVerdict::PRESENT);
        if (v == tnfsFenceVerdict::FAILED)
            CHECK(a.buffered() == 0); // nothing of an unexplained stream is kept
        else
            CHECK(a.unsettled() == tnfsOptionalField::STAT_STRINGS);
    }
}

TEST_CASE("fence: an answer-shaped run received before the fence went out is not its answer")
{
    // A (non-conforming) server sends 00 00 07 23 ff on its own before the fence is sent.
    tnfsReplyAssembler a;
    a.request_sent(6, 0);
    arrive(a, cat(stat_block(6), fence_answer(7)));
    REQUIRE(take(a, request(TNFS_CMD_STAT, 6, FMT_UNKNOWN)).kind == tnfsReplyKind::ANSWER);
    a.request_sent(7, 0);
    tnfsReplyContext fence{0, 7, TNFS_CMD_CLOSE, {}};
    CHECK(a.settle(fence) != tnfsFenceVerdict::ABSENT);
}

TEST_CASE("fence: a retry's duplicate still due keeps replies pending")
{
    tnfsReplyAssembler a;
    a.request_sent(6, 0);
    a.request_sent(6, 0); // timed out and re-sent
    arrive(a, stat_block(6));
    REQUIRE(take(a, request(TNFS_CMD_STAT, 6, FMT_FIXED)).kind == tnfsReplyKind::ANSWER);
    CHECK(a.replies_pending()); // the second copy's reply may come: no fence (RESET instead)
    arrive(a, stat_block(6));
    a.request_sent(7, 0);
    CHECK(take(a, request(TNFS_CMD_OPEN, 7, FMT_FIXED)).kind == tnfsReplyKind::EARLIER);
    CHECK(a.replies_pending()); // the OPEN is still unanswered
    arrive(a, reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {3}, 7));
    CHECK(take(a, request(TNFS_CMD_OPEN, 7, FMT_FIXED)).kind == tnfsReplyKind::ANSWER);
    CHECK_FALSE(a.replies_pending());
}

// --- A format that does not match the server ------------------------------------------------

TEST_CASE("mismatch: EXTENDED against TNFSD never completes a STAT or a failed READ")
{
    // Nothing more comes, so the transaction times out; the stream is then replaced
    // (may_hold_cut_answer) and every retry ends the same way: a failure, not a wrong answer.
    for (const Bytes &r : {stat_block(1), reply(TNFS_CMD_READ, TNFS_RESULT_END_OF_FILE, {}, 1),
                           reply(TNFS_CMD_WRITE, TNFS_RESULT_BAD_FILENUM, {}, 1)})
    {
        tnfsReplyAssembler a;
        tnfsReplyContext c = request(r[3], 1, FMT_EXTENDED);
        a.request_sent(1, 0);
        arrive(a, r);
        CHECK(take(a, c).status == tnfsFrameStatus::NEED_MORE);
        CHECK(a.may_hold_cut_answer(c));
    }
}

TEST_CASE("mismatch: FIXED against paumaed fails loudly when the strings come with the STAT, silently when they are delayed")
{
    // With the STAT: the strings cannot be an answer (they predate the request) -> MALFORMED,
    // a reconnect, and paumaed's lost session (#2008's symptom). Never wrong data.
    {
        tnfsReplyAssembler a;
        a.request_sent(1, 0);
        arrive(a, cat(stat_block(1), strings("fntest", "users")));
        REQUIRE(take(a, request(TNFS_CMD_STAT, 1, FMT_FIXED)).kind == tnfsReplyKind::ANSWER);
        CHECK(a.unsettled() == tnfsOptionalField::NONE);
        a.request_sent(2, 0);
        CHECK(take(a, request(TNFS_CMD_CLOSE, 2, FMT_FIXED)).status == tnfsFrameStatus::MALFORMED);
    }
    // Delayed past the next request and shaped like its answer: taken for it. FMT_FIXED must
    // therefore never be configured for a server that sends the strings.
    {
        tnfsReplyAssembler a;
        a.request_sent('x' - 1, 0);
        arrive(a, stat_block('x' - 1, S41));
        REQUIRE(take(a, request(TNFS_CMD_STAT, 'x' - 1, FMT_FIXED, S41)).kind == tnfsReplyKind::ANSWER);
        a.request_sent('x', 0);
        arrive(a, strings("A", "x#"));
        Taken t = take(a, request(TNFS_CMD_CLOSE, 'x', FMT_FIXED, S41));
        CHECK(t.kind == tnfsReplyKind::ANSWER);
        CHECK(t.reply == strings("A", "x#")); // wrong: the documented cost of a wrong profile
    }
}
