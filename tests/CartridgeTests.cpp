#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "device/Cartridge/Cartridge.h"

// Expected frames are built straight from FujiBusPacket: each companion
// firmware parses its dialect byte for byte.

enum class Reply { Ack, Nak, None };

class FakeBus : public SystemBusBase
{
public:
    std::vector<ByteBuffer> sent;
    std::deque<Reply> replies; // ACK once exhausted

    success_is_true writeFujiBusPacket(FujiBusPacket &packet) override
    {
        sent.push_back(packet.serialize());
        RETURN_SUCCESS_AS_TRUE();
    }

    std::unique_ptr<FujiBusPacket> readFujiBusPacket(int) override
    {
        Reply r = Reply::Ack;
        if (!replies.empty())
        {
            r = replies.front();
            replies.pop_front();
        }
        if (r == Reply::None)
            return nullptr;
        return std::make_unique<FujiBusPacket>(FUJI_DEVICEID::DBC,
                                               r == Reply::Ack ? CMD::FUJI_ACK : CMD::FUJI_NAK);
    }

    void transaction_accept(transState_t) override {}
    void transaction_success() override {}
    void transaction_error() override {}
    success_is_true transaction_get(void *, size_t) override { RETURN_SUCCESS_AS_TRUE(); }
    void transaction_send(const void *, size_t, bool) override {}
};

static std::string pattern(size_t size, uint8_t seed)
{
    std::string s(size, '\0');
    for (size_t i = 0; i < size; i++)
        s[i] = static_cast<char>(seed + i * 7);
    return s;
}

// `fail_at`: offset at which read() starts returning 0
static Cartridge::Source source_of(const std::string &bytes, uint32_t fail_at = UINT32_MAX)
{
    return Cartridge::Source{
        static_cast<uint32_t>(bytes.size()),
        [bytes, fail_at](uint32_t offset, uint8_t *buffer, size_t length) -> size_t {
            if (offset >= fail_at || offset >= bytes.size())
                return 0;
            size_t n = std::min(length, bytes.size() - offset);
            memcpy(buffer, bytes.data() + offset, n);
            return n;
        }};
}

template <typename... Args>
static ByteBuffer frame(fujiCommandID_t command, Args &&...args)
{
    return FujiBusPacket(FUJI_DEVICEID::DBC, command, std::forward<Args>(args)...).serialize();
}

static ByteBuffer stream_open(uint8_t id, uint32_t size)
{
    struct __attribute__((packed))
    {
        uint8_t id;
        u32le_t size;
    } hdr;
    hdr.id = id;
    hdr.size = size;
    return frame(CMD::NET_OPEN, std::string(reinterpret_cast<const char *>(&hdr), sizeof(hdr)));
}

static ByteBuffer segment_open()
{
    return frame(CMD::NET_OPEN, std::uint16_t{0});
}

static void expect_writes(std::vector<ByteBuffer> &want, const std::string &bytes, size_t block)
{
    for (size_t off = 0; off < bytes.size(); off += block)
        want.push_back(frame(CMD::NET_WRITE, bytes.substr(off, block)));
}

TEST_CASE("PICO_STREAMS: image only, 512-byte blocks")
{
    FakeBus bus;
    Cartridge cart(bus, Cartridge::PICO_STREAMS);
    std::string image = pattern(1300, 1);

    CHECK(cart.load(source_of(image), nullptr).is_success());

    std::vector<ByteBuffer> want{stream_open(0, 1300)};
    expect_writes(want, image, 512);
    want.push_back(frame(CMD::NET_CLOSE));
    CHECK(bus.sent == want);
}

TEST_CASE("PICO_STREAMS: map goes first on stream 1")
{
    FakeBus bus;
    Cartridge cart(bus, Cartridge::PICO_STREAMS);
    std::string image = pattern(600, 3), map = pattern(10, 9);
    auto map_src = source_of(map);

    CHECK(cart.load(source_of(image), &map_src).is_success());

    std::vector<ByteBuffer> want{stream_open(1, 10)};
    expect_writes(want, map, 512);
    want.push_back(frame(CMD::NET_CLOSE));
    want.push_back(stream_open(0, 600));
    expect_writes(want, image, 512);
    want.push_back(frame(CMD::NET_CLOSE));
    CHECK(bus.sent == want);
}

TEST_CASE("PICO_STREAMS: short read aborts the CLOSE")
{
    FakeBus bus;
    Cartridge cart(bus, Cartridge::PICO_STREAMS);
    std::string image = pattern(1300, 5);

    CHECK(cart.load(source_of(image, 512), nullptr).is_error());

    std::vector<ByteBuffer> want{stream_open(0, 1300)};
    expect_writes(want, image.substr(0, 512), 512);
    want.push_back(frame(CMD::NET_CLOSE, std::string(1, '\x01')));
    CHECK(bus.sent == want);
}

TEST_CASE("PICO_STREAMS: a NAKed WRITE stops the push and aborts")
{
    FakeBus bus;
    bus.replies = {Reply::Ack, Reply::Ack, Reply::Nak};
    Cartridge cart(bus, Cartridge::PICO_STREAMS);
    std::string image = pattern(1300, 7);

    CHECK(cart.load(source_of(image), nullptr).is_error());

    std::vector<ByteBuffer> want{stream_open(0, 1300)};
    expect_writes(want, image.substr(0, 1024), 512);
    want.push_back(frame(CMD::NET_CLOSE, std::string(1, '\x01')));
    CHECK(bus.sent == want);
}

TEST_CASE("PICO_STREAMS: OPEN NAK, rejected CLOSE, empty and failed map")
{
    std::string image = pattern(100, 11);

    SUBCASE("OPEN NAK sends nothing more")
    {
        FakeBus bus;
        bus.replies = {Reply::Nak};
        CHECK(Cartridge(bus, Cartridge::PICO_STREAMS).load(source_of(image), nullptr).is_error());
        CHECK(bus.sent == std::vector<ByteBuffer>{stream_open(0, 100)});
    }
    SUBCASE("rejected CLOSE fails the load")
    {
        FakeBus bus;
        bus.replies = {Reply::Ack, Reply::Ack, Reply::Nak};
        CHECK(Cartridge(bus, Cartridge::PICO_STREAMS).load(source_of(image), nullptr).is_error());
        CHECK(bus.sent.size() == 3);
    }
    SUBCASE("empty image sends nothing")
    {
        FakeBus bus;
        CHECK(Cartridge(bus, Cartridge::PICO_STREAMS).load(source_of(""), nullptr).is_error());
        CHECK(bus.sent.empty());
    }
    SUBCASE("unreadable map stops before the image")
    {
        FakeBus bus;
        auto map_src = source_of("");
        CHECK(Cartridge(bus, Cartridge::PICO_STREAMS).load(source_of(image), &map_src).is_error());
        CHECK(bus.sent.empty());
    }
}

TEST_CASE("FUJIVERSAL: segment 0 param, 256-byte blocks")
{
    FakeBus bus;
    Cartridge cart(bus, Cartridge::FUJIVERSAL);
    std::string image = pattern(600, 13);

    CHECK(cart.load(source_of(image), nullptr).is_success());

    std::vector<ByteBuffer> want{segment_open()};
    expect_writes(want, image, 256);
    want.push_back(frame(CMD::NET_CLOSE));
    CHECK(bus.sent == want);
}

TEST_CASE("FUJIVERSAL: a NAK still counts as taken")
{
    FakeBus bus;
    bus.replies = {Reply::Nak, Reply::Nak, Reply::Nak, Reply::Nak};
    std::string image = pattern(300, 17);

    CHECK(Cartridge(bus, Cartridge::FUJIVERSAL).load(source_of(image), nullptr).is_success());
    CHECK(bus.sent.size() == 4);
}

TEST_CASE("FUJIVERSAL: no reply stops the push, CLOSE still goes out")
{
    std::string image = pattern(600, 19);

    SUBCASE("on a WRITE")
    {
        FakeBus bus;
        bus.replies = {Reply::Ack, Reply::Ack, Reply::None};
        CHECK(Cartridge(bus, Cartridge::FUJIVERSAL).load(source_of(image), nullptr).is_error());

        std::vector<ByteBuffer> want{segment_open()};
        expect_writes(want, image.substr(0, 512), 256);
        want.push_back(frame(CMD::NET_CLOSE));
        CHECK(bus.sent == want);
    }
    SUBCASE("on OPEN nothing more is sent")
    {
        FakeBus bus;
        bus.replies = {Reply::None};
        CHECK(Cartridge(bus, Cartridge::FUJIVERSAL).load(source_of(image), nullptr).is_error());
        CHECK(bus.sent.size() == 1);
    }
    SUBCASE("empty image sends nothing")
    {
        FakeBus bus;
        CHECK(Cartridge(bus, Cartridge::FUJIVERSAL).load(source_of(""), nullptr).is_error());
        CHECK(bus.sent.empty());
    }
}

TEST_CASE("a bus without a companion fails the load")
{
    class NoCompanion : public FakeBus
    {
    public:
        success_is_true writeFujiBusPacket(FujiBusPacket &packet) override
        {
            return SystemBusBase::writeFujiBusPacket(packet);
        }
    } bus;
    CHECK(Cartridge(bus, Cartridge::FUJIVERSAL).load(source_of("abc"), nullptr).is_error());
}
