#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include "harness_server.h"
#include "tnfslib_stream_link.h"

// tnfslibMountInfo.cpp (the reply buffer allocator) and the link's clock link against the
// stand-ins in tests/tnfs_harness/; the link itself only sees ScriptedStream below.
HarnessSystem fnSystem;
HarnessServer harness_server;

using Bytes = std::vector<uint8_t>;

namespace
{
// A transport whose received bytes, open result and peer close are set by the test.
class ScriptedStream : public tnfsStreamTransport
{
public:
    success_is_true open(const char *, in_addr_t &address, uint16_t, int) override
    {
        opens++;
        if (!accept)
            RETURN_ERROR_AS_FALSE();
        address = 0x0100007F;
        _open = true;
        peer_closed = false;
        incoming.clear();
        RETURN_SUCCESS_AS_TRUE();
    }
    bool is_open() override
    {
        if (_open && peer_closed && incoming.empty())
            _open = false;
        return _open;
    }
    size_t available() override { return _open ? incoming.size() : 0; }
    int read(uint8_t *buf, size_t size) override
    {
        read_sizes.push_back(size);
        size_t n = std::min(size, incoming.size());
        std::copy(incoming.begin(), incoming.begin() + n, buf);
        incoming.erase(incoming.begin(), incoming.begin() + n);
        return static_cast<int>(n);
    }
    size_t write(const uint8_t *buf, size_t size) override
    {
        if (!_open)
            return 0;
        size_t accepted = std::min(size, write_limit);
        Bytes request(buf, buf + accepted);
        written.insert(written.end(), request.begin(), request.end());
        if (on_write)
            on_write(request);
        return accepted;
    }
    void close() override
    {
        closes++;
        _open = false;
        incoming.clear();
    }

    void arrive(const Bytes &b) { incoming.insert(incoming.end(), b.begin(), b.end()); }

    bool accept = true;
    bool peer_closed = false;
    size_t write_limit = SIZE_MAX;
    std::function<void(const Bytes &)> on_write; // the server's side: may arrive() an answer
    std::deque<uint8_t> incoming;
    Bytes written;
    std::vector<size_t> read_sizes;
    int opens = 0;
    int closes = 0;

private:
    bool _open = false;
};

Bytes reply(uint8_t command, uint8_t status, const Bytes &data = {}, uint8_t seq = 1)
{
    Bytes r = {0xEF, 0xBE, seq, command, status};
    r.insert(r.end(), data.begin(), data.end());
    return r;
}

Bytes read_reply(size_t count, uint8_t seq = 1)
{
    Bytes data(2 + count, 0x5A);
    data[0] = static_cast<uint8_t>(count & 0xFF);
    data[1] = static_cast<uint8_t>(count >> 8);
    return reply(TNFS_CMD_READ, TNFS_RESULT_SUCCESS, data, seq);
}

Bytes stat_reply(uint8_t seq) { return reply(TNFS_CMD_STAT, TNFS_RESULT_SUCCESS, Bytes(22, 0x11), seq); }

Bytes strings(const char *uid, const char *gid)
{
    Bytes b(uid, uid + strlen(uid) + 1);
    b.insert(b.end(), gid, gid + strlen(gid) + 1);
    return b;
}

struct Fixture
{
    ScriptedStream stream;
    tnfsMountInfo mount{"tnfs.test"};
    tnfsStreamLink link{stream, mount};
    uint8_t out[TNFS_MAX_REPLY_SIZE];

    Fixture()
    {
        mount.session = 0xBEEF;
        fnSystem.now_us = 0;
    }

    tnfsPacket packet(uint8_t command, uint8_t seq) const
    {
        tnfsPacket p;
        p.session_idl = 0xEF;
        p.session_idh = 0xBE;
        p.sequence_num = seq;
        p.command = command;
        p.payload[0] = 0x03;
        return p;
    }
    success_is_true send(uint8_t command = TNFS_CMD_CLOSE, uint8_t seq = 1)
    { return link.send(packet(command, seq), TNFS_HEADER_SIZE + 1); }
    tnfsStreamReply recv(uint8_t command = TNFS_CMD_CLOSE, uint8_t seq = 1)
    { return link.recv(out, packet(command, seq)); }
    Bytes taken(const tnfsStreamReply &r) const { return Bytes(out, out + r.length); }
    tnfsReplyAssemblerPtr &replies() { return mount.tcp_replies; }
};

const Bytes REQUEST = {0xEF, 0xBE, 0x01, TNFS_CMD_CLOSE, 0x03};
} // namespace

TEST_CASE("send connects once, with a new empty buffer, and writes the whole request")
{
    Fixture f;
    CHECK(f.replies() == nullptr);
    REQUIRE(f.send().is_success());
    CHECK(f.stream.opens == 1);
    CHECK(f.mount.host_ip == 0x0100007F); // a connect by name stores the address it reached
    REQUIRE(f.replies() != nullptr);
    CHECK(f.replies()->buffered() == 0);
    CHECK(f.stream.written == REQUEST);

    REQUIRE(f.send().is_success()); // still connected: no reconnect, same buffer
    CHECK(f.stream.opens == 1);
    CHECK(f.stream.written.size() == 2 * REQUEST.size());
}

TEST_CASE("a failed connect sends nothing and leaves no buffer")
{
    Fixture f;
    f.stream.accept = false;
    CHECK(f.send().is_error());
    CHECK(f.stream.written.empty());
    CHECK(f.replies() == nullptr);
    CHECK(f.recv().status == tnfsStreamStatus::PENDING);
}

TEST_CASE("a short write fails the send")
{
    Fixture f;
    f.stream.write_limit = 2;
    CHECK(f.send().is_error());
    CHECK(f.stream.written == Bytes(REQUEST.begin(), REQUEST.begin() + 2));
}

TEST_CASE("without a buffer, recv touches nothing")
{
    Fixture f;
    f.stream.arrive(reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS));
    CHECK(f.recv().status == tnfsStreamStatus::PENDING);
    CHECK(f.stream.read_sizes.empty());
}

TEST_CASE("one answer per recv: whole, split across reads, or behind an earlier reply")
{
    Fixture f;
    REQUIRE(f.send(TNFS_CMD_CLOSE, 1).is_success());
    Bytes close = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 1);
    f.stream.arrive(close);
    tnfsStreamReply r = f.recv(TNFS_CMD_CLOSE, 1);
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == close);

    REQUIRE(f.send(TNFS_CMD_READ, 2).is_success());
    Bytes read = read_reply(300, 2);
    f.stream.arrive(Bytes(read.begin(), read.begin() + 100));
    CHECK(f.recv(TNFS_CMD_READ, 2).status == tnfsStreamStatus::PENDING);
    f.stream.arrive(Bytes(read.begin() + 100, read.end()));
    r = f.recv(TNFS_CMD_READ, 2);
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == read);

    // A late reply to an earlier request and the answer, in one read: only the answer.
    REQUIRE(f.send(TNFS_CMD_READ, 3).is_success());
    Bytes answer = read_reply(10, 3);
    Bytes both = read_reply(20, 2);
    both.insert(both.end(), answer.begin(), answer.end());
    f.stream.arrive(both);
    r = f.recv(TNFS_CMD_READ, 3);
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == answer);
    CHECK(f.recv(TNFS_CMD_READ, 3).status == tnfsStreamStatus::PENDING);
}

TEST_CASE("recv reads no more than one reply's worth, nor more than the buffer can take")
{
    Fixture f;
    REQUIRE(f.send(TNFS_CMD_READ, 1).is_success());
    Bytes big = read_reply(TNFS_MAX_REPLY_SIZE - 7, 1);
    f.stream.arrive(big);
    f.stream.arrive(big); // the retry's duplicate, behind it
    tnfsStreamReply r = f.recv(TNFS_CMD_READ, 1);
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == big);
    REQUIRE(f.stream.read_sizes.size() == 1);
    CHECK(f.stream.read_sizes[0] == TNFS_MAX_REPLY_SIZE); // `out` is the read buffer
    CHECK(f.stream.incoming.size() == 2 * big.size() - TNFS_MAX_REPLY_SIZE);
    CHECK(f.replies()->buffered() == TNFS_MAX_REPLY_SIZE - static_cast<int>(big.size()));
}

TEST_CASE("a malformed reply drops the connection and the buffer")
{
    Fixture f;
    REQUIRE(f.send().is_success());
    Bytes junk = reply(0x99, TNFS_RESULT_SUCCESS);
    junk.insert(junk.end(), REQUEST.begin(), REQUEST.end());
    f.stream.arrive(junk);
    CHECK(f.recv().status == tnfsStreamStatus::LOST);
    CHECK(f.stream.closes == 1);
    CHECK(f.stream.incoming.empty());
    CHECK(f.replies() == nullptr);
}

TEST_CASE("a connection closed by the server is lost once its bytes are read")
{
    Fixture f;
    REQUIRE(f.send().is_success());
    Bytes close = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS);
    f.stream.arrive(close);
    f.stream.peer_closed = true;
    tnfsStreamReply r = f.recv(); // what arrived before the close is still delivered
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == close);
    CHECK(f.recv().status == tnfsStreamStatus::LOST);
    CHECK(f.replies() == nullptr);

    // Part of a reply, then the close.
    REQUIRE(f.send(TNFS_CMD_CLOSE, 2).is_success());
    CHECK(f.stream.opens == 2);
    Bytes close2 = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 2);
    f.stream.arrive(Bytes(close2.begin(), close2.begin() + 3));
    f.stream.peer_closed = true;
    CHECK(f.recv(TNFS_CMD_CLOSE, 2).status == tnfsStreamStatus::PENDING);
    CHECK(f.recv(TNFS_CMD_CLOSE, 2).status == tnfsStreamStatus::LOST);
}

TEST_CASE("a reconnect starts with an empty buffer")
{
    Fixture f;
    REQUIRE(f.send().is_success());
    Bytes close = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS);
    Bytes with_stray = close;
    with_stray.insert(with_stray.end(), close.begin(), close.begin() + 3);
    f.stream.arrive(with_stray);
    f.stream.peer_closed = true;
    REQUIRE(f.recv().status == tnfsStreamStatus::REPLY);
    REQUIRE(f.replies()->buffered() == 3); // stray bytes from the closed connection

    REQUIRE(f.send(TNFS_CMD_CLOSE, 2).is_success()); // the closed connection is replaced
    CHECK(f.stream.opens == 2);
    CHECK(f.replies()->buffered() == 0);
    Bytes close2 = reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 2);
    f.stream.arrive(close2);
    tnfsStreamReply r = f.recv(TNFS_CMD_CLOSE, 2);
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == close2);
}

TEST_CASE("expire drops what could become a wrong answer, and keeps an idle connection")
{
    Fixture f;
    REQUIRE(f.send(TNFS_CMD_READ, 1).is_success());
    f.link.expire(f.packet(TNFS_CMD_READ, 1));
    CHECK(f.stream.closes == 0);
    CHECK(f.replies() != nullptr);

    Bytes read = read_reply(10, 1);
    f.stream.arrive(Bytes(read.begin(), read.begin() + 6));
    CHECK(f.recv(TNFS_CMD_READ, 1).status == tnfsStreamStatus::PENDING);
    f.link.expire(f.packet(TNFS_CMD_READ, 1));
    CHECK(f.stream.closes == 1);
    CHECK(f.replies() == nullptr);
}

TEST_CASE("drop closes and frees, and is safe to repeat")
{
    Fixture f;
    REQUIRE(f.send().is_success());
    f.link.drop();
    f.link.drop();
    CHECK(f.stream.closes == 2);
    CHECK(f.replies() == nullptr);
    CHECK(f.recv().status == tnfsStreamStatus::PENDING);
}

// --- the reply format rules, on any transport ------------------------------------------

TEST_CASE("a reply received before its request went out is never its answer")
{
    Fixture f;
    REQUIRE(f.send(TNFS_CMD_CLOSE, 1).is_success());
    Bytes early = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x55}, 2); // shaped as the next answer
    f.stream.arrive(reply(TNFS_CMD_CLOSE, TNFS_RESULT_SUCCESS, {}, 1));
    f.stream.arrive(early);
    REQUIRE(f.recv(TNFS_CMD_CLOSE, 1).status == tnfsStreamStatus::REPLY);
    REQUIRE(f.send(TNFS_CMD_OPEN, 2).is_success());
    CHECK(f.recv(TNFS_CMD_OPEN, 2).status == tnfsStreamStatus::PENDING); // skipped, not taken
    Bytes open = reply(TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, {0x03}, 2);
    f.stream.arrive(open);
    tnfsStreamReply r = f.recv(TNFS_CMD_OPEN, 2);
    REQUIRE(r.status == tnfsStreamStatus::REPLY);
    CHECK(f.taken(r) == open);
}

TEST_CASE("an unknown STAT field is settled by a fence before anything else is sent")
{
    for (bool present : {false, true})
    {
        CAPTURE(present);
        Fixture f;
        f.mount.current_sequence_num = 2; // the fence takes the next sequence
        // The server: answers the fence (session 0) with INVALID, after the strings if it sends them.
        f.stream.on_write = [&f, present](const Bytes &w)
        {
            if (w[0] == 0 && w[1] == 0)
                f.stream.arrive({0x00, 0x00, w[2], w[3], 0xFF});
            else if (w[3] == TNFS_CMD_STAT)
            {
                f.stream.arrive(stat_reply(w[2]));
                if (present)
                    f.stream.arrive(strings("fntest", "users"));
            }
        };
        REQUIRE(f.send(TNFS_CMD_STAT, 1).is_success());
        tnfsStreamReply r = f.recv(TNFS_CMD_STAT, 1);
        REQUIRE(r.status == tnfsStreamStatus::REPLY);
        CHECK(f.taken(r) == stat_reply(1));
        CHECK(f.mount.tcp_reply_format.stat_strings == (present ? tnfsFieldUse::PRESENT : tnfsFieldUse::ABSENT));
        CHECK(f.stream.written.size() == 2 * REQUEST.size()); // the STAT and its fence
        CHECK(f.stream.written[REQUEST.size() + 2] == 2);     // the fence's sequence
        CHECK(f.replies()->buffered() == 0);
        CHECK(f.stream.closes == 0);
    }
}

TEST_CASE("a fence that is not answered: the connection is replaced, never guessed")
{
    Fixture f;
    f.stream.on_write = [&f](const Bytes &w)
    {
        if (w[3] == TNFS_CMD_STAT && w[0] != 0)
            f.stream.arrive(stat_reply(w[2]));
    };
    REQUIRE(f.send(TNFS_CMD_STAT, 1).is_success());
    tnfsStreamReply r = f.recv(TNFS_CMD_STAT, 1);
    REQUIRE(r.status == tnfsStreamStatus::REPLY); // the answer stands
    CHECK(f.mount.tcp_fence_failed);
    CHECK(f.mount.tcp_reply_format.stat_strings == tnfsFieldUse::UNKNOWN);
    CHECK(f.stream.closes == 1);
    CHECK(f.replies() == nullptr);
    CHECK(fnSystem.millis() >= static_cast<uint64_t>(f.mount.timeout_ms));
}

TEST_CASE("RESET replaces the connection instead of sending a fence")
{
    Fixture f;
    f.mount.tcp_format_policy = tnfsFormatPolicy::RESET;
    f.stream.on_write = [&f](const Bytes &w) { f.stream.arrive(stat_reply(w[2])); };
    REQUIRE(f.send(TNFS_CMD_STAT, 1).is_success());
    REQUIRE(f.recv(TNFS_CMD_STAT, 1).status == tnfsStreamStatus::REPLY);
    CHECK(f.stream.written.size() == REQUEST.size()); // no fence
    CHECK(f.stream.closes == 1);
    CHECK_FALSE(f.mount.tcp_fence_failed);
}

TEST_CASE("INFER (test builds only) leaves the field to the next reply, as the earlier rule did")
{
    Fixture f;
    f.mount.tcp_format_policy = tnfsFormatPolicy::INFER;
    f.stream.on_write = [&f](const Bytes &w) { f.stream.arrive(stat_reply(w[2])); };
    REQUIRE(f.send(TNFS_CMD_STAT, 1).is_success());
    REQUIRE(f.recv(TNFS_CMD_STAT, 1).status == tnfsStreamStatus::REPLY);
    CHECK(f.stream.written.size() == REQUEST.size());
    CHECK(f.replies()->unsettled() == tnfsOptionalField::STAT_STRINGS);
    CHECK(f.stream.closes == 0);
}
