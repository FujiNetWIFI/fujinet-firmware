#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sio/sioNetwork.h"
#include "sioNetworkStubs.h"
#include "NParser.h"

// Exposes the handlers and the channel's protocol, so a test can drive one
// command at a time and read back what went to the bus.
class TestableSioNetwork : public sioNetwork
{
public:
    using sioNetwork::fujidev_close;
    using sioNetwork::fujidev_status;
    using sioNetwork::fujidev_read;
    using sioNetwork::fujidev_set_sequence;
    using sioNetwork::read_length;
    using sioNetwork::_protocol;
    using sioNetwork::_parser;
};

// --------------------------------------------------------------------------------
// SIOV sends a command that got ERROR once more (DRETRY). CLOSE drops the
// protocol on the first try, so the repeat must not turn the error into success.
// --------------------------------------------------------------------------------

// Closes the way an HTTP PUT the server refused does.
class RefusingProtocol : public NetworkProtocol
{
public:
    RefusingProtocol() : NetworkProtocol(nullptr, nullptr, nullptr) {}
    fujiError_t close() override
    {
        error = NDEV_STATUS::ACCESS_DENIED;
        return FUJI_ERROR::UNSPECIFIED;
    }
    size_t available() override { return 0; }
};

TEST_CASE("a CLOSE with nothing open succeeds")
{
    TestableSioNetwork dev;
    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_CLOSE;

    bus = {};
    dev.fujidev_close(packet);

    CHECK(bus.success == 1);
    CHECK(bus.error == 0);
}

TEST_CASE("a failed CLOSE fails again when SIO repeats it, and STATUS says why")
{
    TestableSioNetwork dev;
    dev._protocol = std::make_unique<RefusingProtocol>();
    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_CLOSE;

    bus = {};
    dev.fujidev_close(packet);
    dev.fujidev_close(packet); // the repeat
    CHECK(bus.error == 2);
    CHECK(bus.success == 0);

    packet.frame.comnd = CMD::NET_STATUS;
    dev.fujidev_status(packet);
    REQUIRE(bus.sent.size() == sizeof(NDeviceStatus));
    CHECK((uint8_t) bus.sent[3] == (uint8_t) NDEV_STATUS::ACCESS_DENIED);
}

// --------------------------------------------------------------------------------
// SIOV reads the data frame after ERROR too. A failed READ that sends ERROR
// alone leaves it waiting out DTIMLO and retrying before it reports the error.
// --------------------------------------------------------------------------------

TEST_CASE("a failed READ sends ERROR with a data frame of the length asked for")
{
    TestableSioNetwork dev; // no channel open, so the READ fails

    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_READ;
    packet.frame.aux1 = 7;
    packet.frame.aux2 = 0;

    bus = {};
    dev.fujidev_read(packet);

    CHECK(bus.error == 0);
    CHECK(bus.sent.size() == 7);
    CHECK(bus.sent_error);
}

// --------------------------------------------------------------------------------
// NET_SET_SEQUENCE: a READ that SIOV retries is the same frame, so with bit 15
// as a sequence bit the retry gets the chunk it missed, not the next one.
// --------------------------------------------------------------------------------

// Hands out the stream "ABCDEFGH..." and counts the reads that reach it.
class StreamParser : public NParser
{
public:
    StreamParser() : NParser(nullptr) {}
    fujiError_t read(std::string &buffer, size_t length) override
    {
        reads++;
        if (fail)
            return FUJI_ERROR::UNSPECIFIED;
        buffer.clear();
        for (size_t i = 0; i < length; i++)
            buffer += (char)('A' + pos++);
        return FUJI_ERROR::NONE;
    }
    int pos = 0;
    int reads = 0;
    bool fail = false;
};

static std::string read_frame(TestableSioNetwork &dev, uint16_t aux)
{
    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_READ;
    packet.frame.aux1 = aux & 0xFF;
    packet.frame.aux2 = aux >> 8;
    dev.fujidev_read(packet);
    return bus.sent;
}

static void set_sequence(TestableSioNetwork &dev, uint8_t on)
{
    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_SET_SEQUENCE;
    packet.frame.aux1 = on;
    packet.frame.aux2 = 0;
    dev.fujidev_set_sequence(packet);
}

TEST_CASE("without NET_SET_SEQUENCE a repeated READ takes the next chunk")
{
    TestableSioNetwork dev;
    dev._parser = std::make_unique<StreamParser>();

    bus = {};
    CHECK(read_frame(dev, 2) == "AB");
    CHECK(read_frame(dev, 2) == "CD");
}

TEST_CASE("a READ that repeats the sequence bit and length gets the same chunk")
{
    TestableSioNetwork dev;
    auto parser = std::make_unique<StreamParser>();
    StreamParser *p = parser.get();
    dev._parser = std::move(parser);

    bus = {};
    set_sequence(dev, 1);
    CHECK(bus.success == 1);

    CHECK(read_frame(dev, 0x0002) == "AB");
    CHECK(read_frame(dev, 0x0002) == "AB"); // the retry
    CHECK(read_frame(dev, 0x8002) == "CD");
    CHECK(read_frame(dev, 0x8002) == "CD"); // the retry
    CHECK(read_frame(dev, 0x0002) == "EF");
    CHECK(read_frame(dev, 0x0003) == "GHI"); // same bit, new length: a new READ
    CHECK(p->reads == 4);
    CHECK_FALSE(bus.sent_error);
}

TEST_CASE("a retried READ that failed fails again without reading")
{
    TestableSioNetwork dev;
    auto parser = std::make_unique<StreamParser>();
    StreamParser *p = parser.get();
    p->fail = true;
    dev._parser = std::move(parser);

    bus = {};
    set_sequence(dev, 1);
    CHECK(read_frame(dev, 0x0004) == std::string(4, '\0'));
    CHECK(bus.sent_error);

    p->fail = false;
    bus.sent_error = false;
    CHECK(read_frame(dev, 0x0004) == std::string(4, '\0')); // the retry
    CHECK(bus.sent_error);
    CHECK(p->reads == 1);

    CHECK(read_frame(dev, 0x8004) == "ABCD");
}

TEST_CASE("CLOSE ends sequence mode, so bit 15 is a length again")
{
    TestableSioNetwork dev;

    bus = {};
    set_sequence(dev, 1);
    CHECK(dev.read_length(0x8002) == 2);

    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_CLOSE;
    dev.fujidev_close(packet);
    CHECK(dev.read_length(0x8002) == 0x8002);

    set_sequence(dev, 1);
    set_sequence(dev, 0);
    CHECK(dev.read_length(0x8002) == 0x8002);
}
