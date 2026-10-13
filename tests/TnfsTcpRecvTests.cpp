#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bus.h"
#include "fnSystem.h"
#include "harness_server.h"
#include "tnfslib.h"
#include "tnfslib_framing.h"

// The real TNFS client (lib/TNFSlib) over the stand-ins in tests/tnfs_harness/.
HarnessSystem fnSystem;
HarnessBus SYSTEM_BUS;
HarnessServer harness_server;

using Bytes = HarnessServer::Bytes;

namespace
{
constexpr uint32_t FILE_SIZE = 1000;
const std::vector<std::string> DIR_NAMES = {"one.atr", "two.atr", "Juegos_Calaluis"};

uint8_t file_byte(uint32_t pos) { return static_cast<uint8_t>(pos * 7 + 3); }

void put_le(Bytes &b, uint32_t value, int size)
{
    for (int i = 0; i < size; i++)
        b.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Enough of TNFSD for mount, stat, open, read and readdirx of one file and one directory.
class FakeTnfsd
{
public:
    // How each reply reaches the client: whole, by default.
    std::function<void(const Bytes &reply)> transmit = [](const Bytes &r)
    { harness_server.deliver(r); };
    std::vector<uint8_t> commands; // one entry per request received

    void handle(const Bytes &request)
    {
        uint8_t command = request[3];
        commands.push_back(command);
        Bytes reply = {0xEF, 0xBE, request[2], command, TNFS_RESULT_SUCCESS};
        switch (command)
        {
        case TNFS_CMD_MOUNT:
            reply.insert(reply.end(), {0x02, 0x01, 0x64, 0x00}); // version 1.2, retry 100 ms
            break;
        case TNFS_CMD_STAT:
            put_le(reply, 0100644, 2); // mode
            put_le(reply, 0, 4);       // uid, gid
            put_le(reply, FILE_SIZE, 4);
            put_le(reply, 0, 12); // atime, mtime, ctime
            break;
        case TNFS_CMD_OPEN:
            reply.push_back(3);
            _read_pos = 0;
            break;
        case TNFS_CMD_READ:
            add_read_data(reply, request[5] | request[6] << 8);
            break;
        case TNFS_CMD_OPENDIRX:
            reply.insert(reply.end(), {5, static_cast<uint8_t>(DIR_NAMES.size()), 0});
            break;
        case TNFS_CMD_READDIRX:
            reply.insert(reply.end(), {static_cast<uint8_t>(DIR_NAMES.size()),
                                       TNFS_READDIRX_STATUS_EOF, 0, 0});
            for (const auto &name : DIR_NAMES)
            {
                reply.push_back(0);           // flags
                put_le(reply, 100 + name.size(), 4); // size
                put_le(reply, 0, 8);          // mtime, ctime
                reply.insert(reply.end(), name.begin(), name.end());
                reply.push_back('\0');
            }
            break;
        default:
            break;
        }
        transmit(reply);
    }

    int count(uint8_t command) const
    { return static_cast<int>(std::count(commands.begin(), commands.end(), command)); }

private:
    void add_read_data(Bytes &reply, uint32_t wanted)
    {
        uint32_t n = std::min(wanted, FILE_SIZE - _read_pos);
        if (n == 0)
        {
            reply[4] = TNFS_RESULT_END_OF_FILE;
            return;
        }
        put_le(reply, n, 2);
        for (uint32_t i = 0; i < n; i++)
            reply.push_back(file_byte(_read_pos++));
    }

    uint32_t _read_pos = 0;
};

// A mount of the fake server over a fresh harness and clock.
struct Session
{
    FakeTnfsd server;
    tnfsMountInfo info{"tnfs.test"};

    explicit Session(uint8_t protocol = TNFS_PROTOCOL_TCP)
    {
        harness_server = HarnessServer();
        harness_server.on_request = [this](const Bytes &r) { server.handle(r); };
        fnSystem.now_us = 0;
        info.protocol = protocol;
    }
};

uint32_t stat_size(tnfsMountInfo &info)
{
    tnfsStat st{};
    REQUIRE(tnfs_stat(&info, &st, "/game.atr") == TNFS_RESULT_SUCCESS);
    return st.filesize;
}

void check_file(tnfsMountInfo &info)
{
    int16_t handle = -1;
    int opened = tnfs_open(&info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle);
    REQUIRE(opened == TNFS_RESULT_SUCCESS);
    Bytes got;
    uint8_t buf[100];
    uint16_t n = 0;
    while (tnfs_read(&info, handle, buf, sizeof(buf), &n) == TNFS_RESULT_SUCCESS && n > 0)
        got.insert(got.end(), buf, buf + n);
    REQUIRE(got.size() == FILE_SIZE);
    for (uint32_t i = 0; i < FILE_SIZE; i++)
        REQUIRE(got[i] == file_byte(i));
}

void check_dir(tnfsMountInfo &info)
{
    REQUIRE(tnfs_opendirx(&info, "/") == TNFS_RESULT_SUCCESS);
    for (const auto &name : DIR_NAMES)
    {
        tnfsStat st{};
        char entry[64];
        REQUIRE(tnfs_readdirx(&info, &st, entry, sizeof(entry)) == TNFS_RESULT_SUCCESS);
        CHECK(std::string(entry) == name);
        CHECK(st.filesize == 100 + name.size());
    }
}

// Mount, stat, read the file and list the directory, checking every result.
void run_session(Session &s)
{
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    CHECK(s.info.session == 0xBEEF);
    CHECK(stat_size(s.info) == FILE_SIZE);
    check_file(s.info);
    check_dir(s.info);
}

// Transmit only the first transmission of `command` differently.
void first_of(Session &s, uint8_t command, std::function<void(const Bytes &)> once)
{
    auto pending = std::make_shared<bool>(true);
    s.server.transmit = [command, once, pending](const Bytes &r)
    {
        if (*pending && r[3] == command)
        {
            *pending = false;
            once(r);
        }
        else
            harness_server.deliver(r);
    };
}

Bytes slice(const Bytes &b, size_t from, size_t to = SIZE_MAX)
{ return Bytes(b.begin() + from, b.begin() + std::min(to, b.size())); }

Bytes concat(Bytes a, const Bytes &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// `reply`'s header with another status and data.
Bytes status_reply(const Bytes &reply, uint8_t status, const Bytes &data)
{ return concat({reply[0], reply[1], reply[2], reply[3], status}, data); }
} // namespace

TEST_CASE("TCP: a reply in one read")
{
    Session s;
    run_session(s);
    CHECK(harness_server.tcp_connects == 1);
    REQUIRE(s.info.tcp_replies != nullptr);
    CHECK(s.info.tcp_replies->buffered() == 0);
}

TEST_CASE("TCP: replies split across reads, at several chunk sizes")
{
    for (size_t chunk : {1, 2, 3, 5, 7, 64, 300})
    {
        CAPTURE(chunk);
        Session s;
        s.server.transmit = [chunk](const Bytes &r)
        {
            for (size_t at = 0; at < r.size(); at += chunk)
                harness_server.deliver(slice(r, at, at + chunk), 1 + at / chunk);
        };
        run_session(s);
        CHECK(harness_server.tcp_connects == 1);
    }
}

TEST_CASE("TCP: READ, READDIRX and STAT replies split at every byte")
{
    for (uint8_t command : {TNFS_CMD_STAT, TNFS_CMD_READ, TNFS_CMD_READDIRX})
    {
        Session probe;
        Bytes sample;
        first_of(probe, command, [&sample](const Bytes &r)
                 { sample = r, harness_server.deliver(r); });
        run_session(probe);
        for (size_t split = 1; split < sample.size(); split++)
        {
            CAPTURE(command);
            CAPTURE(split);
            Session s;
            first_of(s, command, [split](const Bytes &r)
                     {
                         harness_server.deliver(slice(r, 0, split));
                         harness_server.deliver(slice(r, split), 3);
                     });
            run_session(s);
            CHECK(harness_server.tcp_connects == 1);
        }
    }
}

TEST_CASE("TCP: several replies in one read")
{
    // Every reply arrives with a duplicate: the next request finds the duplicate first,
    // whole or cut across reads, and must skip it by sequence number.
    for (size_t cut : {size_t(0), size_t(3)})
    {
        CAPTURE(cut);
        Session s;
        s.server.transmit = [cut](const Bytes &r)
        {
            Bytes both = concat(r, r);
            size_t first = cut ? r.size() + cut : both.size();
            harness_server.deliver(slice(both, 0, first));
            if (first < both.size())
                harness_server.deliver(slice(both, first));
        };
        run_session(s);
        CHECK(harness_server.tcp_connects == 1);
    }
}

TEST_CASE("TCP: a late reply, then the retry's duplicate")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r) { harness_server.deliver(r, 2500); });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_STAT) == 2); // the timeout re-sent it
    CHECK(stat_size(s.info) == FILE_SIZE);     // skips the retry's reply
    check_file(s.info);
    CHECK(harness_server.tcp_connects == 1);
}

TEST_CASE("TCP: a reply with another sequence number is skipped")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 Bytes stale = r, early = r;
                 stale[2]--;
                 early[2] += 5;
                 stale[11] = early[11] = 0x55; // a different size, so a mix-up would show
                 harness_server.deliver(concat(concat(stale, early), r));
             });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_STAT) == 1);

    // Nothing but a wrong sequence number: time out, then retry.
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 Bytes stale = r;
                 stale[2]--;
                 harness_server.deliver(stale);
             });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_STAT) == 3);
    CHECK(harness_server.tcp_connects == 1);
}

TEST_CASE("TCP: a reply cut off by a timeout is not completed by the retry's reply")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             { harness_server.deliver(slice(r, 0, 10)); });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_STAT) == 2);
    CHECK(harness_server.tcp_connects == 2); // the retry went out on a new connection
    check_file(s.info);
}

TEST_CASE("TCP: the server closes the connection in the middle of a reply")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 harness_server.deliver(slice(r, 0, 12));
                 harness_server.tcp_closing = true;
             });
    uint64_t start = fnSystem.millis();
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(harness_server.tcp_connects == 2);
    CHECK(fnSystem.millis() - start < static_cast<uint64_t>(s.info.timeout_ms)); // no timeout
    check_dir(s.info);
}

TEST_CASE("TCP: bytes left on a closed connection are not read as part of the next reply")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 harness_server.deliver(concat(r, slice(r, 0, 3)));
                 harness_server.tcp_closing = true;
             });
    CHECK(stat_size(s.info) == FILE_SIZE);
    REQUIRE(s.info.tcp_replies != nullptr);
    CHECK(s.info.tcp_replies->buffered() == 3);
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(harness_server.tcp_connects == 2);
    check_file(s.info);
}

TEST_CASE("TCP: a malformed reply drops the connection and the retry reconnects")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 Bytes junk = r;
                 junk[3] = 0x99; // no such command
                 harness_server.deliver(concat(junk, r));
             });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_STAT) == 2);
    CHECK(harness_server.tcp_connects == 2);
    check_dir(s.info);
}

TEST_CASE("TCP: a failed MOUNT carries the version, EAGAIN carries the back-off")
{
    Session s;
    const Bytes version = {0x02, 0x01};
    first_of(s, TNFS_CMD_MOUNT, [version](const Bytes &r)
             { harness_server.deliver(status_reply(r, TNFS_RESULT_ACCESS_DENIED, version)); });
    CHECK(tnfs_mount(&s.info) == TNFS_RESULT_ACCESS_DENIED);
    REQUIRE(s.info.tcp_replies != nullptr);
    CHECK(s.info.tcp_replies->buffered() == 0); // both version bytes were consumed
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);

    const Bytes backoff_ms = {0x20, 0x00};
    first_of(s, TNFS_CMD_STAT, [backoff_ms](const Bytes &r)
             { harness_server.deliver(status_reply(r, TNFS_RESULT_TRY_AGAIN, backoff_ms)); });
    uint64_t start = fnSystem.millis();
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(fnSystem.millis() - start >= 0x20); // waited as asked
    CHECK(s.server.count(TNFS_CMD_STAT) == 2);
    CHECK(s.info.tcp_replies->buffered() == 0);
    CHECK(harness_server.tcp_connects == 1);
    check_file(s.info);
}

TEST_CASE("UDP: each datagram is still one reply, and TCP is not used")
{
    Session s(TNFS_PROTOCOL_UDP);
    run_session(s);
    // Trailing bytes in a datagram are ignored, as before.
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             { harness_server.deliver(concat(r, {0xAA, 0xBB})); });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(harness_server.tcp_connects == 0);
    CHECK(harness_server.udp_requests == static_cast<int>(s.server.commands.size()));
    CHECK(s.info.tcp_replies == nullptr);
}

TEST_CASE("Unknown protocol: falls back to UDP as before")
{
    SUBCASE("TCP connection refused")
    {
        Session s(TNFS_PROTOCOL_UNKNOWN);
        harness_server.tcp_accepts = false;
        run_session(s);
        CHECK(s.info.protocol == TNFS_PROTOCOL_UDP);
        CHECK(harness_server.tcp_connects == 1);
        CHECK(s.info.tcp_replies == nullptr);
    }
    SUBCASE("TCP accepted but never answered")
    {
        Session s(TNFS_PROTOCOL_UNKNOWN);
        harness_server.on_request = [&s](const Bytes &r)
        {
            if (harness_server.udp_requests > 0)
                s.server.handle(r);
        };
        run_session(s);
        CHECK(s.info.protocol == TNFS_PROTOCOL_UDP);
        CHECK(harness_server.tcp_connects == 1);
        CHECK(s.info.tcp_replies == nullptr); // the silent connection was closed
    }
    SUBCASE("TCP answers")
    {
        Session s(TNFS_PROTOCOL_UNKNOWN);
        run_session(s);
        CHECK(s.info.protocol == TNFS_PROTOCOL_TCP);
        CHECK(harness_server.udp_requests == 0);
    }
}
