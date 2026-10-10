// lib/hotsync: framing checked against bytes captured from a Palm OS 3.3
// device, and the sync flow checked against a fake device.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "hotsync/Dlp.h"
#include "hotsync/DlpClient.h"
#include "hotsync/HotSyncSession.h"
#include "hotsync/NetSyncTransport.h"
#include "hotsync/PadpTransport.h"
#include "hotsync/PalmDatabase.h"
#include "hotsync/Slp.h"

#include <deque>
#include <map>
#include <string>

static ByteBuffer hex(const std::string &s)
{
    ByteBuffer out;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}

// Plays back device output in order and captures everything written.
class ScriptedLink : public HotSyncLink
{
public:
    std::deque<ByteBuffer> reads;
    ByteBuffer written;
    uint32_t baud = 9600;

    int read(uint8_t *buf, size_t len, uint32_t) override
    {
        if (reads.empty())
            return -1;
        ByteBuffer &chunk = reads.front();
        size_t n = std::min(len, chunk.size());
        std::memcpy(buf, chunk.data(), n);
        chunk.erase(chunk.begin(), chunk.begin() + n);
        if (chunk.empty())
            reads.pop_front();
        return static_cast<int>(n);
    }
    int write(const uint8_t *buf, size_t len) override
    {
        written.insert(written.end(), buf, buf + len);
        return static_cast<int>(len);
    }
    void set_baud_rate(uint32_t b) override { baud = b; }
};

// Captured at the start of a sync: a stale ABORT, loopback probes, then WAKEUP.
static const char *PALM_WAKEUP =
    "beefed0303020004ffa508000000f593beefed03030300000baef9abbeefed03030300000caf701d"
    "beefed030302000effaf01c0000a0100010200000001c20086b1";
static const char *PALM_ACK_OF_INIT = "beefed0303020004ffa502c0000a1a85";
static const char *HOST_ACK_OF_WAKEUP = "beefed0303020004ffa502c0000a1a85";
static const char *HOST_CMP_INIT = "beefed030302000effaf01c0000a0290010100000001c20003b0";

TEST_CASE("SLP frame encoding matches a captured PADP ACK")
{
    SlpDatagram ack;
    ack.xid = 0xFF;
    ack.payload = hex("02c0000a");
    CHECK(slp_encode(ack) == hex(HOST_ACK_OF_WAKEUP));
}

TEST_CASE("SLP reader skips noise and rejects a corrupt frame")
{
    ScriptedLink link;
    ByteBuffer corrupt = hex(PALM_ACK_OF_INIT);
    corrupt[12] ^= 0xFF;
    ByteBuffer stream = hex("00beef11");
    stream.insert(stream.end(), corrupt.begin(), corrupt.end());
    ByteBuffer good = hex(PALM_ACK_OF_INIT);
    stream.insert(stream.end(), good.begin(), good.end());
    link.reads.push_back(stream);

    SlpReader reader(link);
    SlpDatagram datagram;
    REQUIRE(reader.read(datagram, 10).is_success());
    CHECK(datagram.xid == 0xFF);
    CHECK(datagram.payload == hex("02c0000a"));
    CHECK(reader.read(datagram, 10).is_error());
}

TEST_CASE("CMP handshake reproduces the captured exchange and raises the baud rate")
{
    ScriptedLink link;
    link.reads.push_back(hex(PALM_WAKEUP));
    link.reads.push_back(hex(PALM_ACK_OF_INIT));

    PadpTransport padp(link);
    REQUIRE(padp.accept().is_success());

    ByteBuffer expected = hex(HOST_ACK_OF_WAKEUP);
    ByteBuffer init = hex(HOST_CMP_INIT);
    expected.insert(expected.end(), init.begin(), init.end());
    CHECK(link.written == expected);
    CHECK(padp.baud_rate() == 115200);
    CHECK(link.baud == 115200);
}

TEST_CASE("Waiting for WAKEUP stops before INIT, so a cradle can lift its listening deadline")
{
    ScriptedLink link;
    link.reads.push_back(hex(PALM_WAKEUP));
    link.reads.push_back(hex(PALM_ACK_OF_INIT));

    PadpTransport padp(link);
    REQUIRE(padp.wait_for_wakeup().is_success());
    CHECK(link.written == hex(HOST_ACK_OF_WAKEUP));
    CHECK(link.reads.size() == 1);

    REQUIRE(padp.answer_wakeup().is_success());
    CHECK(link.reads.empty());
    CHECK(link.baud == 115200);
}

TEST_CASE("A retransmitted WAKEUP is not taken as the ACK of our INIT")
{
    // WAKEUP, the same WAKEUP again (our ACK was "lost"), then the real ACK.
    ByteBuffer wakeup = hex("beefed030302000effaf01c0000a0100010200000001c20086b1");
    ScriptedLink link;
    link.reads.push_back(wakeup);
    link.reads.push_back(wakeup);
    link.reads.push_back(hex(PALM_ACK_OF_INIT));

    PadpTransport padp(link);
    REQUIRE(padp.accept().is_success());
    // Had the duplicate been read as an ACK, the real ACK would still be unread.
    uint8_t leftover;
    CHECK(link.read(&leftover, 1, 0) == -1);
}

TEST_CASE("PADP reassembles a multi-fragment message and ACKs each fragment")
{
    ScriptedLink link;
    auto fragment = [](uint8_t flags, uint16_t size_or_offset, const ByteBuffer &data) {
        SlpDatagram d;
        d.xid = 7;
        d.payload = {0x01, flags, static_cast<uint8_t>(size_or_offset >> 8),
                     static_cast<uint8_t>(size_or_offset)};
        d.payload.insert(d.payload.end(), data.begin(), data.end());
        return slp_encode(d);
    };
    link.reads.push_back(fragment(0x80, 5, hex("0102")));
    link.reads.push_back(fragment(0x40, 2, hex("030405")));

    PadpTransport padp(link);
    ByteBuffer message;
    REQUIRE(padp.receive(message, 10).is_success());
    CHECK(message == hex("0102030405"));

    SlpDatagram ack_first, ack_last;
    ack_first.xid = ack_last.xid = 7;
    ack_first.payload = hex("02c00005");
    ack_last.payload = hex("02c00002");
    ByteBuffer expected = slp_encode(ack_first);
    ByteBuffer second = slp_encode(ack_last);
    expected.insert(expected.end(), second.begin(), second.end());
    CHECK(link.written == expected);
}

TEST_CASE("NetSync handshake and first request match palm-sync's recorded session")
{
    ScriptedLink link;
    link.reads.push_back(hex("01ff0000001690010000000000000020000000080200000000000000"));
    link.reads.push_back(hex("0101000000329201000000000000002000000024ffffffff3c003c000000004000000002"
                             "c0a80205e06b0000000000000000000000000000"));
    link.reads.push_back(hex("0102000000089300000000000000"));

    NetSyncTransport netsync(link);
    REQUIRE(netsync.accept().is_success());
    DlpRequest request(DlpFunc::ReadSysInfo);
    request.arg(DlpArgWriter().u16(1).u16(4));
    REQUIRE(netsync.send(request.encode()).is_success());

    CHECK(link.written ==
          hex("0101000000321201000000000000002000000024ffffffff3c003c000000000000000000c0a801210427"
              "0000000000000000000000000000"
              "01020000002e1301000000000000002000000020ffffffff003c003c0000000000000001000000000000"
              "00000000000000000000"
              "0103000000081201200400010004"));
}

TEST_CASE("DLP request encoding matches the captured ReadSysInfo")
{
    DlpRequest request(DlpFunc::ReadSysInfo);
    request.arg(DlpArgWriter().u16(1).u16(4));
    CHECK(request.encode() == hex("1201200400010004"));
}

TEST_CASE("DLP response decoding handles small and short arguments")
{
    // funcId|0x80, argc 2, no error; a small arg 0x20 then a short arg 0x21.
    ByteBuffer reply = hex("92020000" "2004" "03303000" "a1000003" "aabbcc");

    DlpResponse response;
    REQUIRE(response.decode(DlpFunc::ReadSysInfo, reply).is_success());
    CHECK(response.ok());
    CHECK(response.arg(0) == hex("03303000"));
    CHECK(response.arg(1) == hex("aabbcc"));
    CHECK(response.decode(DlpFunc::ReadUserInfo, reply).is_error());
}

TEST_CASE("Palm timestamps convert both ways")
{
    DlpDateTime t{2026, 9, 30, 17, 5, 9};
    uint32_t seconds = palm_seconds_from_date(t);
    CHECK(seconds == 3873632709u);
    DlpDateTime back = palm_date_from_seconds(seconds);
    CHECK(back.year == 2026);
    CHECK(back.month == 9);
    CHECK(back.day == 30);
    CHECK(back.hour == 17);
    CHECK(back.minute == 5);
    CHECK(back.second == 9);
    CHECK(palm_date_from_seconds(0).year == 1904);
}

static PalmDatabase sample_record_db()
{
    PalmDatabase db;
    db.info.name = "FujiTest";
    std::memcpy(db.info.type, "DATA", 4);
    std::memcpy(db.info.creator, "FjNt", 4);
    db.info.flags = DLP_DB_BACKUP;
    db.app_info = hex("a1a2a3");
    DlpRecord live;
    live.id = 0x123456;
    live.category = 3;
    live.data = hex("10111213");
    DlpRecord archived;
    archived.id = 0x000102;
    archived.attributes = 0x08; // archive
    archived.data = hex("20");
    db.records = {live, archived};
    return db;
}

TEST_CASE("PDB files round-trip, keeping categories and the archive bit")
{
    PalmDatabase db = sample_record_db();
    ByteBuffer file = db.serialize();

    PalmDatabase parsed;
    REQUIRE(parsed.parse(file).is_success());
    CHECK(parsed.info.name == "FujiTest");
    CHECK(std::string(parsed.info.creator) == "FjNt");
    CHECK(parsed.app_info == hex("a1a2a3"));
    REQUIRE(parsed.records.size() == 2);
    CHECK(parsed.records[0].id == 0x123456);
    CHECK(parsed.records[0].category == 3);
    CHECK(parsed.records[0].data == hex("10111213"));
    CHECK((parsed.records[1].attributes & 0x08) != 0);
    CHECK(parsed.records[1].data == hex("20"));
    CHECK(parsed.serialize() == file);
}

TEST_CASE("Backup file names are safe on FAT")
{
    DlpDbInfo info;
    info.name = "Graffiti/Short:Cuts";
    CHECK(hotsync_backup_file_name(info) == "Graffiti_Short_Cuts.pdb");
    info.flags = DLP_DB_RESOURCE;
    CHECK(hotsync_backup_file_name(info) == "Graffiti_Short_Cuts.prc");
}

// Answers DLP requests like a device holding one database.
class FakePalm : public DlpTransport
{
public:
    std::vector<DlpFunc> calls;
    std::map<std::string, PalmDatabase> databases;
    PalmDatabase *open = nullptr;
    std::string user_name;
    uint8_t written_mod_flags = 0;
    std::vector<std::string> log;

    success_is_true accept() override { RETURN_SUCCESS_AS_TRUE(); }

    success_is_true send(const ByteBuffer &message) override
    {
        DlpArgReader header(message);
        _func = static_cast<DlpFunc>(header.u8());
        calls.push_back(_func);
        size_t argc = header.u8();
        _arg.clear();
        if (argc > 0)
        {
            uint8_t id = header.u8();
            size_t len = (id & 0x80) ? (header.skip(1), header.u16()) : header.u8();
            _arg = header.bytes(len);
        }
        RETURN_SUCCESS_AS_TRUE();
    }

    success_is_true receive(ByteBuffer &message, uint32_t) override
    {
        DlpArgWriter reply;
        uint16_t error = respond(reply);
        message = {static_cast<uint8_t>(static_cast<uint8_t>(_func) | 0x80),
                   static_cast<uint8_t>(reply.data().empty() ? 0 : 1),
                   static_cast<uint8_t>(error >> 8), static_cast<uint8_t>(error)};
        if (!reply.data().empty())
        {
            DlpRequest wrapped(_func);
            wrapped.arg(reply);
            ByteBuffer encoded = wrapped.encode();
            message.insert(message.end(), encoded.begin() + 2, encoded.end());
        }
        RETURN_SUCCESS_AS_TRUE();
    }

private:
    uint16_t respond(DlpArgWriter &reply)
    {
        DlpArgReader in(_arg);
        switch (_func)
        {
        case DlpFunc::ReadSysInfo:
            reply.u32(0x03303000).u32(0).u8(0).u8(0);
            return 0;
        case DlpFunc::ReadUserInfo:
            reply.u32(0).u32(0).u32(0);
            for (int i = 0; i < 16; ++i)
                reply.u8(0);
            reply.u8(0).u8(0);
            return 0;
        case DlpFunc::DeleteDB:
            in.skip(2);
            return databases.erase(in.cstring()) ? 0 : 5;
        case DlpFunc::CreateDB: {
            PalmDatabase db;
            ByteBuffer creator = in.bytes(4), type = in.bytes(4);
            std::memcpy(db.info.creator, creator.data(), 4);
            std::memcpy(db.info.type, type.data(), 4);
            in.skip(2);
            db.info.flags = in.u16();
            db.info.version = in.u16();
            db.info.name = in.cstring();
            open = &(databases[db.info.name] = db);
            reply.u8(1);
            return 0;
        }
        case DlpFunc::WriteResource: {
            in.skip(2);
            DlpResource r;
            ByteBuffer type = in.bytes(4);
            std::memcpy(r.type, type.data(), 4);
            r.id = in.u16();
            r.data = in.bytes(in.u16());
            open->resources.push_back(r);
            return 0;
        }
        case DlpFunc::ReadDBList:
            return 5; // nothing to back up
        case DlpFunc::WriteUserInfo:
            in.skip(12 + 8);
            written_mod_flags = in.u8();
            in.skip(1);
            user_name = in.cstring();
            return 0;
        case DlpFunc::AddSyncLogEntry:
            log.push_back(in.cstring());
            return 0;
        default:
            return 0;
        }
    }

    DlpFunc _func = DlpFunc::EndOfSync;
    ByteBuffer _arg;
};

class MemoryStorage : public HotSyncStorage
{
public:
    std::map<std::string, ByteBuffer> install;
    std::vector<std::string> installed;

    std::vector<std::string> pending_installs() override
    {
        std::vector<std::string> names;
        for (auto &entry : install)
            names.push_back(entry.first);
        return names;
    }
    success_is_true read_install(const std::string &name, ByteBuffer &out) override
    {
        out = install[name];
        RETURN_SUCCESS_AS_TRUE();
    }
    success_is_true mark_installed(const std::string &name) override
    {
        installed.push_back(name);
        RETURN_SUCCESS_AS_TRUE();
    }
    success_is_true write_backup(const std::string &, const std::string &, const ByteBuffer &) override
    {
        RETURN_SUCCESS_AS_TRUE();
    }
};

TEST_CASE("A sync installs queued apps and names a new device")
{
    PalmDatabase app;
    app.info.name = "Hello";
    std::memcpy(app.info.type, "appl", 4);
    std::memcpy(app.info.creator, "HeLo", 4);
    app.info.flags = DLP_DB_RESOURCE;
    DlpResource code;
    std::memcpy(code.type, "code", 4);
    code.id = 1;
    code.data = ByteBuffer(3000, 0x4E);
    app.resources = {code};

    MemoryStorage storage;
    storage.install["hello.prc"] = app.serialize();
    storage.install["readme.txt"] = hex("00");

    FakePalm palm;
    HotSyncOptions options;
    options.default_user_name = "FujiNet";
    options.new_user_id = 42;
    options.backup = HotSyncBackup::FLAGGED;
    HotSyncReport report = HotSyncSession(palm, storage, options).run();

    CHECK(report.error == DlpError::NONE);
    CHECK(report.installed == 1);
    CHECK(report.user_name == "FujiNet");
    REQUIRE(palm.databases.count("Hello") == 1);
    REQUIRE(palm.databases["Hello"].resources.size() == 1);
    CHECK(palm.databases["Hello"].resources[0].data.size() == 3000);
    CHECK(storage.installed == std::vector<std::string>{"hello.prc"});
    CHECK(palm.user_name == "FujiNet");
    CHECK((palm.written_mod_flags & (DLP_MOD_USER_ID | DLP_MOD_USER_NAME)) ==
          (DLP_MOD_USER_ID | DLP_MOD_USER_NAME));
    CHECK(palm.calls.back() == DlpFunc::EndOfSync);
}
