#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bus.h"
#include "fnSystem.h"
#include "harness_server.h"
#include "tnfslib.h"
#include "tnfslib_framing.h"
#include "tnfslib_internal.h" // _tnfs_transaction, for probe_format()

// The real TNFS client (lib/TNFSlib) over the stand-ins in tests/tnfs_harness/.
HarnessSystem fnSystem;
HarnessBus SYSTEM_BUS;
HarnessServer harness_server;

using Bytes = HarnessServer::Bytes;

namespace
{
// Settle every unknown field after MOUNT (STAT, plus a READ and a WRITE the server refuses).
// Test helper only: the firmware settles fields at their first use.
bool probe_format(tnfsMountInfo *m)
{
    auto known = [m]()
    {
        const tnfsReplyFormat &f = m->tcp_reply_format;
        auto ok = [m](tnfsFieldUse u, int bit)
        { return u == tnfsFieldUse::PRESENT || (u == tnfsFieldUse::ABSENT && (m->tcp_format_verified & bit)); };
        return ok(f.stat_strings, 1) && ok(f.read_count, 2) && ok(f.write_count, 4);
    };
    tnfsStat st;
    tnfs_stat(m, &st, "/");
    for (uint8_t command : {TNFS_CMD_READ, TNFS_CMD_WRITE})
    {
        tnfsPacket packet;
        packet.command = command;
        packet.payload[0] = 0xFF; // handle
        packet.payload[1] = 1;
        packet.payload[2] = 0;
        _tnfs_transaction(m, packet, 3);
    }
    return known();
}

constexpr uint32_t FILE_SIZE = 1000;
const std::vector<std::string> DIR_NAMES = {"one.atr", "two.atr", "Games_Directory"};

uint8_t file_byte(uint32_t pos) { return static_cast<uint8_t>(pos * 7 + 3); }

void put_le(Bytes &b, uint32_t value, int size)
{
    for (int i = 0; i < size; i++)
        b.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Enough of TNFSD for mount, stat, open, read and readdirx of one file and one directory.
class FakeTNFSd
{
public:
    // How each reply reaches the client: whole, by default.
    std::function<void(const Bytes &reply)> transmit = [](const Bytes &r)
    { harness_server.deliver(r); };
    std::vector<uint8_t> commands; // one entry per request received

    // paumaed's reply shapes, both allowed by the protocol: STAT ends with the uid and gid
    // strings, and a READ that fails (EOF, an error, INVALID) still carries its 2-byte count.
    std::string uid_string, gid_string;
    bool stat_strings = false;
    bool error_count = false;
    // paumaed also forgets a session when its TCP connection closes.
    bool forget_on_close = false;
    int invalid_replies = 0;
    uint16_t session_id = 0xBEEF;
    uint8_t close_status = TNFS_RESULT_SUCCESS;
    uint8_t write_status = TNFS_RESULT_SUCCESS;

    // The client's fence: TNFSD and paumaed answer INVALID with its session, sequence and
    // command (paumaed's READ adds its count).
    enum class Foreign
    {
        ECHO,
        SILENT,
        AS_OWN,
        LATE,   // echoed 2.5 s later, past the client's 2 s wait
        TWICE,  // echoed twice
        HANGUP, // the server closes the connection instead
    } foreign = Foreign::ECHO;
    int fences = 0; // requests for another session; not in `commands`

    void handle(const Bytes &request)
    {
        uint8_t command = request[3];
        uint16_t sid = static_cast<uint16_t>(request[0] | request[1] << 8);
        if (command != TNFS_CMD_MOUNT && sid != session_id && foreign != Foreign::AS_OWN)
        {
            fences++;
            if (foreign == Foreign::SILENT)
                return;
            if (foreign == Foreign::HANGUP)
            {
                harness_server.tcp_closing = true;
                return;
            }
            Bytes invalid = {request[0], request[1], request[2], command, TNFS_RESULT_INVALID_HANDLE};
            if (command == TNFS_CMD_READ && error_count)
                put_le(invalid, 0, 2);
            if (foreign == Foreign::LATE)
                return harness_server.deliver(invalid, 2500);
            transmit(invalid);
            if (foreign == Foreign::TWICE)
                transmit(invalid);
            return;
        }
        commands.push_back(command);
        Bytes reply = {static_cast<uint8_t>(session_id), static_cast<uint8_t>(session_id >> 8),
                       request[2], command, TNFS_RESULT_SUCCESS};
        if (command == TNFS_CMD_MOUNT)
            _mounted_on = harness_server.tcp_connects;
        else if (forget_on_close && _mounted_on != harness_server.tcp_connects)
        {
            invalid_replies++;
            reply[4] = TNFS_RESULT_INVALID_HANDLE;
            if (command == TNFS_CMD_READ && error_count)
                put_le(reply, 0, 2);
            transmit(reply);
            return;
        }
        bool on_file = command == TNFS_CMD_READ || command == TNFS_CMD_WRITE ||
                       command == TNFS_CMD_CLOSE || command == TNFS_CMD_LSEEK;
        if (on_file && request[4] != 3) // the only handle OPEN gives out
        {
            reply[4] = TNFS_RESULT_BAD_FILENUM;
            if (error_count && (command == TNFS_CMD_READ || command == TNFS_CMD_WRITE))
                put_le(reply, 0, 2);
            transmit(reply);
            return;
        }
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
            if (stat_strings)
            {
                reply.insert(reply.end(), uid_string.begin(), uid_string.end());
                reply.push_back('\0');
                reply.insert(reply.end(), gid_string.begin(), gid_string.end());
                reply.push_back('\0');
            }
            break;
        case TNFS_CMD_CLOSE:
            reply[4] = close_status;
            break;
        case TNFS_CMD_LSEEK: // handle, whence (SET, CUR, END), signed offset
        {
            int32_t offset = static_cast<int32_t>(request[6] | request[7] << 8 | request[8] << 16 |
                                                  uint32_t(request[9]) << 24);
            int64_t base = request[5] == 0 ? 0 : request[5] == 1 ? _read_pos : FILE_SIZE;
            _read_pos = static_cast<uint32_t>(std::max<int64_t>(0, base + offset));
            put_le(reply, _read_pos, 4);
            break;
        }
        case TNFS_CMD_OPEN:
            reply.push_back(3);
            _read_pos = 0;
            break;
        case TNFS_CMD_READ:
            add_read_data(reply, request[5] | request[6] << 8);
            break;
        case TNFS_CMD_WRITE: // handle, count, data
            reply[4] = write_status;
            if (write_status == TNFS_RESULT_SUCCESS)
                reply.insert(reply.end(), {request[5], request[6]});
            else if (error_count && write_status != TNFS_RESULT_INVALID_HANDLE)
                put_le(reply, 0, 2);
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
        uint32_t n = std::min(wanted, FILE_SIZE - std::min(_read_pos, FILE_SIZE));
        if (n == 0)
        {
            reply[4] = TNFS_RESULT_END_OF_FILE;
            if (error_count)
                put_le(reply, 0, 2);
            return;
        }
        put_le(reply, n, 2);
        for (uint32_t i = 0; i < n; i++)
            reply.push_back(file_byte(_read_pos++));
    }

    uint32_t _read_pos = 0;
    int _mounted_on = 0;
};

// A mount of the fake server over a fresh harness and clock.
struct Session
{
    FakeTNFSd server;
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
        // IDENTIFY: an unaccounted reply between the answer and its fence means the field
        // cannot be settled, so the client reconnects (RESET).
        CHECK(harness_server.tcp_connects == 4);
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
    // the retry's duplicate is still due when the first STAT's strings must be settled,
    // so no fence: a new connection instead (earlier rule: 1). TNFSD moves the session over.
    CHECK(harness_server.tcp_connects == 2);
    CHECK(s.server.fences == 2); // none for the retried STAT; the next STAT and the EOF, on the new connection
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
    // the stale STAT leaves its strings UNKNOWN and only an answer can be fenced, so the
    // client reconnects and asks again (earlier rule: 1).
    CHECK(s.server.count(TNFS_CMD_STAT) == 2);

    // Nothing but a wrong sequence number: time out, then retry.
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 Bytes stale = r;
                 stale[2]--;
                 harness_server.deliver(stale);
             });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_STAT) == 4); // earlier rule: 3
    CHECK(harness_server.tcp_connects == 2);   // earlier rule: 1
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
    // the fence cannot be answered on a closing connection: dropped at once, the three
    // bytes with it (The earlier rule kept them buffered until the next request found the close).
    CHECK(s.info.tcp_replies == nullptr);
    CHECK(stat_size(s.info) == FILE_SIZE);
    // the connection is already gone, so no fence and no verdict; the next STAT is
    // fenced on the new connection (the earlier rule: 2).
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

namespace
{
void paumaed_shapes(Session &s, const std::string &uid = "fntest", const std::string &gid = "users")
{
    s.server.stat_strings = s.server.error_count = s.server.forget_on_close = true;
    s.server.uid_string = uid;
    s.server.gid_string = gid;
}

// What the #2008 log shows after the directory listing: open the image (STAT, then OPEN),
// seek to the end and back, then read it to the end.
void mount_image(tnfsMountInfo &info)
{
    int16_t handle = -1;
    REQUIRE(tnfs_open(&info, "/game.atr", TNFS_OPENMODE_READ, 0x1c0, &handle) == TNFS_RESULT_SUCCESS);
    uint32_t pos = 0;
    REQUIRE(tnfs_lseek(&info, handle, 0, SEEK_END, &pos, true) == TNFS_RESULT_SUCCESS);
    CHECK(pos == FILE_SIZE);
    REQUIRE(tnfs_lseek(&info, handle, 0, SEEK_SET, &pos, true) == TNFS_RESULT_SUCCESS);
    CHECK(pos == 0);
    Bytes got;
    uint8_t buf[100];
    uint16_t n = 0;
    int result;
    while ((result = tnfs_read(&info, handle, buf, sizeof(buf), &n)) == TNFS_RESULT_SUCCESS && n > 0)
        got.insert(got.end(), buf, buf + n);
    CHECK(result == TNFS_RESULT_END_OF_FILE);
    REQUIRE(got.size() == FILE_SIZE);
    for (uint32_t i = 0; i < FILE_SIZE; i++)
        REQUIRE(got[i] == file_byte(i));
    CHECK(tnfs_close(&info, handle) == TNFS_RESULT_SUCCESS);
}

// One connection, one session, nothing re-sent: the framing never got in the way.
void check_undisturbed(Session &s)
{
    CHECK(harness_server.tcp_connects == 1);
    CHECK(s.server.count(TNFS_CMD_MOUNT) == 1);
    CHECK(s.server.invalid_replies == 0);
}
} // namespace

TEST_CASE("TCP: paumaed's STAT strings and READ counts belong to their replies (#2008)")
{
    for (auto names : std::vector<std::pair<std::string, std::string>>{
             {"fntest", "users"}, {"", ""}, {"a", ""}, {"", "g"}, {"root", "wheel"},
             {std::string(200, 'u'), std::string(200, 'g')}})
    {
        CAPTURE(names.first.size());
        CAPTURE(names.second.size());
        Session s;
        paumaed_shapes(s, names.first, names.second);
        run_session(s);
        mount_image(s.info);
        CHECK(stat_size(s.info) == FILE_SIZE);
        check_undisturbed(s);
    }
}

TEST_CASE("TCP: the #2008 sequence against paumaed takes no timeout and no remount")
{
    Session s;
    paumaed_shapes(s);
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    check_dir(s.info);
    uint64_t start = fnSystem.millis();
    mount_image(s.info);
    CHECK(fnSystem.millis() - start < 1000); // #1950: 5 s of min_retry and a remount
    check_undisturbed(s);
}

TEST_CASE("TCP: paumaed's replies split at every byte, the optional fields arriving later")
{
    for (uint8_t command : {TNFS_CMD_STAT, TNFS_CMD_READ})
    {
        Session probe;
        paumaed_shapes(probe);
        Bytes sample;
        auto take_last = [&sample](const Bytes &r) { sample = r, harness_server.deliver(r); };
        probe.server.transmit = [command, take_last](const Bytes &r)
        {
            if (r[3] == command)
                take_last(r);
            else
                harness_server.deliver(r);
        };
        REQUIRE(tnfs_mount(&probe.info) == TNFS_RESULT_SUCCESS);
        mount_image(probe.info); // the last READ reply is the EOF with its count
        REQUIRE(sample.size() > 5);
        for (size_t split = 1; split < sample.size(); split++)
            for (uint32_t later_ms : {0u, 30u, 1500u})
            {
                CAPTURE(command);
                CAPTURE(split);
                CAPTURE(later_ms);
                Session s;
                paumaed_shapes(s);
                s.server.transmit = [command, split, later_ms](const Bytes &r)
                {
                    if (r[3] != command)
                        return harness_server.deliver(r);
                    harness_server.deliver(slice(r, 0, split));
                    harness_server.deliver(slice(r, split), later_ms);
                };
                REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
                mount_image(s.info);
                CHECK(stat_size(s.info) == FILE_SIZE);
                check_undisturbed(s);
            }
    }
}

TEST_CASE("TCP: paumaed's replies coalesced, and a retry's duplicate STAT with strings")
{
    Session s;
    paumaed_shapes(s);
    // every reply doubled with no retry blocks the fence; known as EXTENDED it needs none
    // (IDENTIFY against this server: see "IDENTIFY against a server that doubles every reply").
    s.info.tcp_reply_format = tnfsReplyFormat::extended();
    s.server.transmit = [](const Bytes &r) { harness_server.deliver(concat(r, r)); };
    run_session(s);
    mount_image(s.info);
    check_undisturbed(s);

    Session late;
    paumaed_shapes(late);
    REQUIRE(tnfs_mount(&late.info) == TNFS_RESULT_SUCCESS);
    // Settle the format before a retry can leave a duplicate behind; otherwise the late
    // STAT's strings meet it and the reconnect loses paumaed's session and handles.
    REQUIRE(probe_format(&late.info));
    first_of(late, TNFS_CMD_STAT, [](const Bytes &r) { harness_server.deliver(r, 2500); });
    CHECK(stat_size(late.info) == FILE_SIZE);
    CHECK(late.server.count(TNFS_CMD_STAT) == 3); // the probe's, and the timeout re-sent it
    mount_image(late.info);
    check_undisturbed(late);
}

TEST_CASE("TCP: a timeout with only an optional field buffered keeps the connection")
{
    Session s;
    paumaed_shapes(s);
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    CHECK(stat_size(s.info) == FILE_SIZE); // its strings stay buffered until the next reply
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r) { harness_server.deliver(r, 2500); });
    CHECK(stat_size(s.info) == FILE_SIZE);
    mount_image(s.info);
    check_undisturbed(s);
}

TEST_CASE("TCP: TNFSD's sessionless reply to a request fragment is skipped")
{
    // Captured from TNFSD 26.0915.1 answering the tail of a split request: session 0, the
    // sequence and command bytes taken from the request's path, INVALID.
    const Bytes stray = {0x00, 0x00, 0x61, 0x6d, 0xff};
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [stray](const Bytes &r) { harness_server.deliver(concat(r, stray)); });
    CHECK(stat_size(s.info) == FILE_SIZE);
    check_file(s.info);
    // the stray sits between the STAT and its fence (TNFSD's split-request defect breaks
    // "one reply per request"); the fence fails, and the mount reconnects from then on.
    CHECK(harness_server.tcp_connects == 3);

    // Its sequence byte comes from the request's path, so it may equal ours: still not the
    // answer, which would otherwise read as an expired session and remount.
    Session t;
    REQUIRE(tnfs_mount(&t.info) == TNFS_RESULT_SUCCESS);
    first_of(t, TNFS_CMD_STAT, [](const Bytes &r)
             { harness_server.deliver(concat({0x00, 0x00, r[2], 0x6d, 0xff}, r)); });
    CHECK(stat_size(t.info) == FILE_SIZE);
    CHECK(t.server.count(TNFS_CMD_MOUNT) == 1);
    CHECK(harness_server.tcp_connects == 1);
}

TEST_CASE("TCP: a reply for another command or session is never taken as the answer")
{
    SUBCASE("same sequence, another command (a mixed-up retransmission)")
    {
        Session s;
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
                 { harness_server.deliver({r[0], r[1], r[2], TNFS_CMD_OPEN, TNFS_RESULT_SUCCESS, 7}); });
        CHECK(stat_size(s.info) == FILE_SIZE);
        CHECK(s.server.count(TNFS_CMD_STAT) == 2);
        CHECK(harness_server.tcp_connects == 2);
    }
    SUBCASE("same sequence and command, another session")
    {
        Session s;
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
                 {
                     Bytes other = r;
                     other[0] ^= 0x01;
                     other[11] = 0x55; // a different size, so a mix-up would show
                     harness_server.deliver(other);
                 });
        CHECK(stat_size(s.info) == FILE_SIZE);
        CHECK(s.server.count(TNFS_CMD_STAT) == 2);
        CHECK(harness_server.tcp_connects == 2);
    }
}

namespace
{
// R1: session 0x0041 and uid "A" start the STAT strings like a reply header of ours
// (41 00); a gid of <next sequence><command>[<status>] completes one. The answer to the
// next request then arrives a round trip later, as it does over any real link.
constexpr uint8_t R1_STAT_SEQUENCE = 'x' - 1;

void r1_server(Session &s, const std::string &gid_after_sequence, uint8_t delayed_command,
               uint32_t delay_ms = 30, size_t split = 0)
{
    paumaed_shapes(s, "A", std::string(1, 'x') + gid_after_sequence);
    s.server.session_id = 0x0041;
    s.server.transmit = [delayed_command, delay_ms, split](const Bytes &r)
    {
        if (r[3] != delayed_command)
            return harness_server.deliver(r);
        if (split == 0)
            return harness_server.deliver(r, delay_ms);
        harness_server.deliver(slice(r, 0, split), delay_ms);
        harness_server.deliver(slice(r, split), delay_ms + 1);
    };
}
} // namespace

TEST_CASE("TCP: STAT strings shaped like the next answer are never taken for it (R1)")
{
    SUBCASE("CLOSE: the strings read as its success; the server answers EBADF")
    {
        for (uint32_t delay_ms : {0u, 30u})
        {
            CAPTURE(delay_ms);
            Session s;
            r1_server(s, "#", TNFS_CMD_CLOSE, delay_ms);
            s.server.close_status = TNFS_RESULT_BAD_FILENUM;
            REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
            int16_t handle = -1;
            REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) ==
                    TNFS_RESULT_SUCCESS);
            s.info.current_sequence_num = R1_STAT_SEQUENCE;
            CHECK(stat_size(s.info) == FILE_SIZE);
            CHECK(tnfs_close(&s.info, handle) == TNFS_RESULT_BAD_FILENUM);
            check_undisturbed(s);
        }
    }
    SUBCASE("OPEN: the strings read as its header, the answer's first byte as the handle")
    {
        Session s;
        r1_server(s, ")", TNFS_CMD_OPEN, 30, 1);
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        s.info.current_sequence_num = R1_STAT_SEQUENCE; // tnfs_open: STAT, then OPEN
        int16_t handle = -1;
        REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) ==
                TNFS_RESULT_SUCCESS);
        CHECK(handle == 3);
        check_undisturbed(s);
    }
    SUBCASE("READ: the strings read as its EOF, so the file would come back empty")
    {
        Session s;
        r1_server(s, "!!", TNFS_CMD_READ);
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        int16_t handle = -1;
        REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) ==
                TNFS_RESULT_SUCCESS);
        s.info.current_sequence_num = R1_STAT_SEQUENCE;
        CHECK(stat_size(s.info) == FILE_SIZE);
        Bytes got;
        uint8_t buf[100];
        uint16_t n = 0;
        while (tnfs_read(&s.info, handle, buf, sizeof(buf), &n) == TNFS_RESULT_SUCCESS && n > 0)
            got.insert(got.end(), buf, buf + n);
        REQUIRE(got.size() == FILE_SIZE);
        for (uint32_t i = 0; i < FILE_SIZE; i++)
            REQUIRE(got[i] == file_byte(i));
        check_undisturbed(s);
    }
}

TEST_CASE("TCP: a reply received before its request went out is never its answer")
{
    // With the STAT answer comes a reply shaped as the answer to the OPEN that follows,
    // with the OPEN's sequence (a wrapped-around sequence, or a confused server).
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 Bytes early = {r[0], r[1], static_cast<uint8_t>(r[2] + 1), TNFS_CMD_OPEN,
                                TNFS_RESULT_SUCCESS, 0x55};
                 harness_server.deliver(concat(r, early));
             });
    int16_t handle = -1;
    REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) == TNFS_RESULT_SUCCESS);
    CHECK(handle == 3);
    CHECK(harness_server.tcp_connects == 2); // the early reply blocks the fence (earlier rule: 1)
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

// --- Reply formats -----------------------------------------------------------------------

namespace
{
const tnfsFormatPolicy POLICIES[] = {tnfsFormatPolicy::INFER, tnfsFormatPolicy::IDENTIFY,
                                     tnfsFormatPolicy::RESET};

const char *policy_name(tnfsFormatPolicy p)
{
    return p == tnfsFormatPolicy::INFER ? "INFER" : p == tnfsFormatPolicy::IDENTIFY ? "IDENTIFY" : "RESET";
}

enum class Outcome
{
    RIGHT, // the server's answer
    WRONG, // another answer, taken as valid
    FAILURE, // an error the caller sees
};

const char *outcome_name(Outcome o)
{ return o == Outcome::RIGHT ? "RIGHT" : o == Outcome::WRONG ? "WRONG" : "FAILURE"; }

// STAT `stat_seq` answered in two segments: the block, then 10 ms later (after the next
// request) strings shaped like the answer to request 'x', whose real answer comes at 30 ms.
struct WireCase
{
    const char *what;
    uint8_t command;
    uint16_t session;
    std::string uid, gid;
};

const WireCase WIRE_CASES[] = {
    {"CLOSE: success instead of EBADF", TNFS_CMD_CLOSE, 0x0041, "A", "x#"},
    {"OPEN: handle 0x41 instead of 3", TNFS_CMD_OPEN, 0x0041, "A", "x)"},
    {"READ: end of file instead of the data", TNFS_CMD_READ, 0x0041, "A", "x!!"},
    {"WRITE: success instead of ENOSPC", TNFS_CMD_WRITE, 0x4241, "ABx\"", "CD"},
};

Outcome run_wire_case(Session &s, const WireCase &k, uint8_t stat_seq)
{
    paumaed_shapes(s); // fntest/users on every other STAT; paumaed forgets closed sessions
    s.server.session_id = k.session;
    s.server.close_status = TNFS_RESULT_BAD_FILENUM;
    s.server.write_status = TNFS_RESULT_NO_SPACE_ON_DEVICE;
    Bytes mimic = {};
    mimic.insert(mimic.end(), k.uid.begin(), k.uid.end());
    mimic.push_back('\0');
    mimic.insert(mimic.end(), k.gid.begin(), k.gid.end());
    mimic.push_back('\0');
    uint8_t command = k.command;
    s.server.transmit = [stat_seq, mimic, command](const Bytes &r)
    {
        if (r[3] == TNFS_CMD_STAT && r[4] == TNFS_RESULT_SUCCESS && r[2] == stat_seq)
        {
            harness_server.deliver(slice(r, 0, 27));
            harness_server.deliver(mimic, 10);
            return;
        }
        if (r[3] == command && r[0] != 0) // the request after it (not a fence)
        {
            harness_server.deliver(slice(r, 0, 1), 30);
            harness_server.deliver(slice(r, 1), 31);
            return;
        }
        harness_server.deliver(r);
    };
    tnfsFormatPolicy policy = s.info.tcp_format_policy;
    tnfsReplyFormat start = s.info.tcp_reply_format;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    int16_t handle = -1;
    if (k.command != TNFS_CMD_OPEN)
        REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) == TNFS_RESULT_SUCCESS);
    // The critical STAT meets the format as the mount started (UNKNOWN unless configured).
    s.info.tcp_reply_format = start;
    s.info.tcp_format_policy = policy;
    s.info.tcp_fence_failed = false;
    s.info.current_sequence_num = stat_seq;
    if (k.command == TNFS_CMD_OPEN) // tnfs_open: STAT, then OPEN
    {
        int r = tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle);
        if (r != TNFS_RESULT_SUCCESS)
            return Outcome::FAILURE;
        return handle == 3 ? Outcome::RIGHT : Outcome::WRONG;
    }
    tnfsStat st{};
    if (tnfs_stat(&s.info, &st, "/game.atr") != TNFS_RESULT_SUCCESS || st.filesize != FILE_SIZE)
        return Outcome::FAILURE;
    if (k.command == TNFS_CMD_CLOSE)
    {
        int r = tnfs_close(&s.info, handle);
        return r == TNFS_RESULT_BAD_FILENUM ? Outcome::RIGHT : r == TNFS_RESULT_SUCCESS ? Outcome::WRONG : Outcome::FAILURE;
    }
    if (k.command == TNFS_CMD_WRITE)
    {
        uint8_t byte = 0x5A;
        uint16_t n = 0;
        int r = tnfs_write(&s.info, handle, &byte, 1, &n);
        return r == TNFS_RESULT_NO_SPACE_ON_DEVICE ? Outcome::RIGHT
               : r == TNFS_RESULT_SUCCESS          ? Outcome::WRONG
                                                   : Outcome::FAILURE;
    }
    Bytes got;
    uint8_t buf[100];
    uint16_t n = 0;
    int r;
    while ((r = tnfs_read(&s.info, handle, buf, sizeof(buf), &n)) == TNFS_RESULT_SUCCESS && n > 0)
        got.insert(got.end(), buf, buf + n);
    if (r != TNFS_RESULT_END_OF_FILE)
        return Outcome::FAILURE;
    if (got.size() != FILE_SIZE)
        return Outcome::WRONG; // an end of file the server did not send
    for (uint32_t i = 0; i < FILE_SIZE; i++)
        if (got[i] != file_byte(i))
            return Outcome::WRONG;
    return Outcome::RIGHT;
}
} // namespace

TEST_CASE("TCP formats: a delayed STAT field on the wire, by policy and profile")
{
    // gap 1: the strings name the request right after the STAT (the earlier rule's next request, and
    // the slot of IDENTIFY's fence); gap 2: the request after the fence.
    for (const WireCase &k : WIRE_CASES)
        for (int gap : {1, 2})
        {
            uint8_t stat_seq = static_cast<uint8_t>('x' - gap);
            for (tnfsFormatPolicy policy : POLICIES)
            {
                CAPTURE(k.what);
                CAPTURE(gap);
                CAPTURE(policy_name(policy));
                Session s;
                s.info.tcp_format_policy = policy;
                Outcome o = run_wire_case(s, k, stat_seq);
                MESSAGE(std::string(k.what), " | gap ", gap, " | ", std::string(policy_name(policy)), " -> ",
                        std::string(outcome_name(o)),
                        " | connects ", harness_server.tcp_connects, ", mounts ", s.server.count(TNFS_CMD_MOUNT),
                        ", invalid ", s.server.invalid_replies, ", fences ", s.server.fences);
                bool wrong_expected = policy == tnfsFormatPolicy::INFER && gap == 1;
                CHECK((o != Outcome::WRONG || wrong_expected));
                if (policy == tnfsFormatPolicy::INFER && gap == 1)
                    CHECK(o == Outcome::WRONG); // the delayed field taken with the earlier rule
                if (policy == tnfsFormatPolicy::IDENTIFY)
                {
                    CHECK(o == Outcome::RIGHT);
                    check_undisturbed(s);
                    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::PRESENT);
                }
            }
            CAPTURE(k.what);
            CAPTURE(gap);
            Session e; // the profile configured
            e.info.tcp_reply_format = tnfsReplyFormat::extended();
            Outcome o = run_wire_case(e, k, stat_seq);
            MESSAGE(std::string(k.what), " | gap ", gap, " | EXTENDED -> ", std::string(outcome_name(o)),
                    " | connects ",
                    harness_server.tcp_connects, ", fences ", e.server.fences);
            CHECK(o == Outcome::RIGHT);
            check_undisturbed(e);
            CHECK(e.server.fences == 0);
        }
}

TEST_CASE("TCP formats: IDENTIFY learns TNFSD's and paumaed's formats with one fence per field")
{
    Session t;
    run_session(t);
    mount_image(t.info);
    CHECK(t.info.tcp_reply_format.stat_strings == tnfsFieldUse::ABSENT);
    CHECK(t.info.tcp_reply_format.read_count == tnfsFieldUse::ABSENT);
    CHECK(t.info.tcp_reply_format.write_count == tnfsFieldUse::UNKNOWN); // no failed WRITE yet
    CHECK(t.server.fences == 2);
    check_undisturbed(t);

    Session p;
    paumaed_shapes(p);
    run_session(p);
    mount_image(p.info);
    CHECK(p.info.tcp_reply_format.stat_strings == tnfsFieldUse::PRESENT);
    CHECK(p.info.tcp_reply_format.read_count == tnfsFieldUse::PRESENT);
    CHECK(p.server.fences == 2);
    check_undisturbed(p);

    // The format belongs to the server: a new connection keeps it, no new fence.
    first_of(p, TNFS_CMD_STAT, [](const Bytes &r) { harness_server.deliver(slice(r, 0, 10)); });
    CHECK(stat_size(p.info) == FILE_SIZE); // cut off, retried on a new connection
    CHECK(harness_server.tcp_connects == 2);
    CHECK(p.info.tcp_reply_format.stat_strings == tnfsFieldUse::PRESENT);
    CHECK(p.server.fences == 2);
}

TEST_CASE("TCP formats: the probe after MOUNT settles all three fields at once")
{
    for (bool paumaed : {false, true})
    {
        CAPTURE(paumaed);
        Session s;
        if (paumaed)
            paumaed_shapes(s);
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        CHECK(probe_format(&s.info));
        tnfsFieldUse want = paumaed ? tnfsFieldUse::PRESENT : tnfsFieldUse::ABSENT;
        CHECK(s.info.tcp_reply_format.stat_strings == want);
        CHECK(s.info.tcp_reply_format.read_count == want);
        CHECK(s.info.tcp_reply_format.write_count == want);
        CHECK(s.server.fences == 3);
        uint32_t fences = s.server.fences;
        mount_image(s.info);
        CHECK(stat_size(s.info) == FILE_SIZE);
        CHECK(s.server.fences == fences); // nothing left to settle
        check_undisturbed(s);
    }
}

TEST_CASE("TCP formats: a server that does not answer the fence falls back to reconnecting")
{
    for (auto foreign : {FakeTNFSd::Foreign::SILENT, FakeTNFSd::Foreign::AS_OWN})
        for (bool paumaed : {false, true})
        {
            CAPTURE(static_cast<int>(foreign));
            CAPTURE(paumaed);
            Session s;
            if (paumaed)
                paumaed_shapes(s);
            s.server.foreign = foreign;
            REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
            uint64_t start = fnSystem.millis();
            tnfsStat st{};
            int r = tnfs_stat(&s.info, &st, "/game.atr");
            MESSAGE("foreign ", static_cast<int>(foreign), " paumaed ", paumaed, ": STAT ", r, " in ",
                    fnSystem.millis() - start, " ms, connects ", harness_server.tcp_connects);
            CHECK(r == TNFS_RESULT_SUCCESS);
            CHECK(st.filesize == FILE_SIZE);
            CHECK(s.info.tcp_fence_failed);
            CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::UNKNOWN);
            // Never a wrong answer: the next request uses a new stream, which costs paumaed a
            // remount and its open handles.
            int16_t handle = -1;
            r = tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle);
            CHECK(r == TNFS_RESULT_SUCCESS);
            CHECK(handle == 3);
            CHECK(harness_server.tcp_connects >= 2);
        }
}

TEST_CASE("TCP formats: IDENTIFY against a server that doubles every reply never takes a wrong answer")
{
    Session s;
    paumaed_shapes(s);
    s.server.transmit = [](const Bytes &r) { harness_server.deliver(concat(r, r)); };
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    int16_t handle = -1;
    REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) == TNFS_RESULT_SUCCESS);
    Bytes got;
    uint8_t buf[100];
    uint16_t n = 0;
    int r;
    while ((r = tnfs_read(&s.info, handle, buf, sizeof(buf), &n)) == TNFS_RESULT_SUCCESS && n > 0)
        got.insert(got.end(), buf, buf + n);
    MESSAGE("doubled replies: read result ", r, ", ", got.size(), " B, connects ", harness_server.tcp_connects,
            ", mounts ", s.server.count(TNFS_CMD_MOUNT), ", invalid ", s.server.invalid_replies);
    // Every byte delivered is right; the reconnects make paumaed forget the session, and the
    // caller sees an error, not a short file taken as whole.
    for (size_t i = 0; i < got.size(); i++)
        REQUIRE(got[i] == file_byte(static_cast<uint32_t>(i)));
    CHECK((r == TNFS_RESULT_END_OF_FILE ? got.size() == FILE_SIZE : r != TNFS_RESULT_SUCCESS));
}

TEST_CASE("TCP formats: a profile that does not match the server")
{
    SUBCASE("FIXED configured against paumaed: only an expectation, the fence corrects it")
    {
        Session s;
        paumaed_shapes(s);
        s.info.tcp_reply_format = tnfsReplyFormat::fixed();
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        int16_t handle = -1;
        int r = tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle);
        MESSAGE("FIXED configured vs paumaed: OPEN ", r, " handle ", handle, ", connects ",
                harness_server.tcp_connects, ", fences ", s.server.fences);
        CHECK(r == TNFS_RESULT_SUCCESS);
        CHECK(handle == 3);
        CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::PRESENT);
        check_undisturbed(s);
    }
    SUBCASE("FIXED configured against paumaed, a delayed field on all four requests: never taken")
    {
        for (const WireCase &k : WIRE_CASES)
            for (int gap : {1, 2})
            {
                CAPTURE(k.what);
                CAPTURE(gap);
                Session s;
                s.info.tcp_reply_format = tnfsReplyFormat::fixed();
                Outcome o = run_wire_case(s, k, static_cast<uint8_t>('x' - gap));
                MESSAGE("FIXED configured vs paumaed, ", std::string(k.what), " gap ", gap, " -> ",
                        std::string(outcome_name(o)));
                CHECK(o == Outcome::RIGHT);
                check_undisturbed(s);
            }
    }
    SUBCASE("FIXED configured against TNFSD: confirmed field by field, then trusted")
    {
        Session s;
        s.info.tcp_reply_format = tnfsReplyFormat::fixed();
        run_session(s);
        mount_image(s.info);
        CHECK(s.server.fences == 2); // STAT strings and READ count; no failed WRITE yet
        CHECK(s.info.tcp_format_verified == 0x3);
        uint32_t fences = s.server.fences;
        CHECK(stat_size(s.info) == FILE_SIZE);
        mount_image(s.info);
        CHECK(s.server.fences == fences);
        check_undisturbed(s);
    }
    SUBCASE("EXTENDED against TNFSD: STATs and failed READs never complete")
    {
        Session s;
        s.info.tcp_reply_format = tnfsReplyFormat::extended();
        s.info.max_retries = 2; // keep the test short; production has 8
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        uint64_t start = fnSystem.millis();
        tnfsStat st{};
        int r = tnfs_stat(&s.info, &st, "/game.atr");
        uint64_t stat_ms = fnSystem.millis() - start;
        MESSAGE("EXTENDED vs TNFSD: STAT ", r, " after ", stat_ms, " ms and ", s.server.count(TNFS_CMD_STAT),
                " tries, connects ", harness_server.tcp_connects);
        CHECK(r != TNFS_RESULT_SUCCESS); // an error, never a wrong answer
        CHECK(s.server.count(TNFS_CMD_STAT) == 2);
    }
}

// --- The fence's answer late, doubled or refused; mount lifecycle -------------------------

TEST_CASE("TCP fence: a fence answered late never reaches a later request")
{
    for (bool paumaed : {false, true})
    {
        CAPTURE(paumaed);
        Session s;
        if (paumaed)
            paumaed_shapes(s);
        s.server.foreign = FakeTNFSd::Foreign::LATE;
        REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
        CHECK(stat_size(s.info) == FILE_SIZE); // fence unanswered in 2 s: dropped, RESET from now on
        CHECK(s.info.tcp_fence_failed);
        int16_t handle = -1;
        REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) == TNFS_RESULT_SUCCESS);
        CHECK(handle == 3); // the late INVALID was lost with the old connection
        CHECK(s.server.fences == 1);
    }
}

TEST_CASE("TCP fence: a fence answered twice: the second answer is skipped as sessionless")
{
    for (bool paumaed : {false, true})
    {
        CAPTURE(paumaed);
        Session s;
        if (paumaed)
            paumaed_shapes(s);
        s.server.foreign = FakeTNFSd::Foreign::TWICE;
        run_session(s);
        mount_image(s.info);
        CHECK(stat_size(s.info) == FILE_SIZE);
        CHECK_FALSE(s.info.tcp_fence_failed);
        check_undisturbed(s);
    }
}

TEST_CASE("TCP fence: a server that hangs up on the fence")
{
    Session s;
    s.server.foreign = FakeTNFSd::Foreign::HANGUP;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.info.tcp_fence_failed); // its reaction to the fence: no more fences on this mount
    check_file(s.info);
    CHECK(s.server.fences == 1);
    CHECK(harness_server.tcp_connects >= 2);
}

TEST_CASE("TCP fence: no fence on a connection that is already gone, and no verdict either")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    harness_server.tcp_half_closed_writes = true; // as a real socket after the peer's FIN
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r)
             {
                 harness_server.deliver(r);
                 harness_server.tcp_closing = true;
             });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.fences == 0);
    CHECK_FALSE(s.info.tcp_fence_failed); // the next STAT may still be fenced
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.fences == 1);
    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::ABSENT);
}

TEST_CASE("TCP fence: the format survives reconnects and session recovery, not an unmount")
{
    // mount_image closes its file: tnfs_umount does not clear the client's file table (a
    // pre-existing quirk), so a handle left open would come back stale after a remount.
    Session s;
    paumaed_shapes(s); // forgets the session when its connection closes
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    mount_image(s.info);
    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::PRESENT);
    CHECK(s.info.tcp_reply_format.read_count == tnfsFieldUse::PRESENT);
    uint32_t fences = s.server.fences;
    // A reply cut off: the retry goes out on a new connection, paumaed answers INVALID, the
    // session is recovered by a new MOUNT; the format is the server's and is kept.
    first_of(s, TNFS_CMD_STAT, [](const Bytes &r) { harness_server.deliver(slice(r, 0, 10)); });
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.count(TNFS_CMD_MOUNT) == 2);
    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::PRESENT);
    CHECK(s.server.fences == fences);

    // Unmounted, then mounted again on the same object against a server of the other format:
    // learned again, and right.
    CHECK(tnfs_umount(&s.info) == TNFS_RESULT_SUCCESS);
    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::UNKNOWN);
    CHECK(s.info.tcp_format_verified == 0);
    s.server.stat_strings = s.server.error_count = s.server.forget_on_close = false;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    CHECK(stat_size(s.info) == FILE_SIZE);
    mount_image(s.info);
    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::ABSENT);
    CHECK(s.info.tcp_reply_format.read_count == tnfsFieldUse::ABSENT);
}

TEST_CASE("TCP fence: the fence leaves sessions, open files and the retransmit slot alone")
{
    // The fence carries session 0 and handle 0xFF: the fake records nothing for it, and the
    // file opened before it reads back whole afterwards.
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    int16_t handle = -1;
    REQUIRE(tnfs_open(&s.info, "/game.atr", TNFS_OPENMODE_READ, 0, &handle) == TNFS_RESULT_SUCCESS);
    size_t before = s.server.commands.size();
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.commands.size() == before + 1); // the STAT only; the fence is not a command of the session
    Bytes got;
    uint8_t buf[100];
    uint16_t n = 0;
    while (tnfs_read(&s.info, handle, buf, sizeof(buf), &n) == TNFS_RESULT_SUCCESS && n > 0)
        got.insert(got.end(), buf, buf + n);
    REQUIRE(got.size() == FILE_SIZE);
    CHECK(tnfs_close(&s.info, handle) == TNFS_RESULT_SUCCESS);
    check_undisturbed(s);
}

TEST_CASE("TCP fence: a fence whose write fails says nothing about the server")
{
    Session s;
    REQUIRE(tnfs_mount(&s.info) == TNFS_RESULT_SUCCESS);
    harness_server.tcp_fail_write = harness_server.tcp_writes + 2; // the STAT goes out, its fence does not
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.fences == 0);
    CHECK_FALSE(s.info.tcp_fence_failed);
    CHECK(harness_server.tcp_connects == 1); // RESET: dropped, the next request reconnects
    CHECK(stat_size(s.info) == FILE_SIZE);
    CHECK(s.server.fences == 1);
    CHECK(s.info.tcp_reply_format.stat_strings == tnfsFieldUse::ABSENT);
    CHECK(harness_server.tcp_connects == 2);
}
