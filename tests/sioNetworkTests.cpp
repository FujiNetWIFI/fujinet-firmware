#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sio/sioNetwork.h"
#include "sioNetworkStubs.h"
#include "NDevice/NParser.h"
#include "fn_query_flags.h"

// Exposes the handlers and the channel's protocol, so a test can drive one
// command at a time and read back what went to the bus.
class TestableSioNetwork : public sioNetwork
{
public:
    using sioNetwork::fujidev_close;
    using sioNetwork::fujidev_status;
    using sioNetwork::fujidev_read;
    using sioNetwork::fujicore_set_query;
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
// A QUERY's flags byte goes to the parser, which may refuse it.
// --------------------------------------------------------------------------------

// Refuses every flags byte but takes the query itself.
class FlagRefusingParser : public NParser
{
public:
    FlagRefusingParser() : NParser(nullptr) {}
    error_is_true setQueryParam(uint8_t) override { RETURN_ERROR_AS_TRUE(); }
    error_is_true setQuery(const std::string &) override { RETURN_SUCCESS_AS_FALSE(); }
    fujiError_t read(std::string &, size_t) override { return FUJI_ERROR::UNSPECIFIED; }
    fujiError_t write(std::string &) override { return FUJI_ERROR::UNSPECIFIED; }
    size_t available() override { return 0; }
    off_t seek(off_t, int) override { return -1; }
};

TEST_CASE("a QUERY whose flags byte the parser refuses fails (#1697)")
{
    TestableSioNetwork dev;
    dev._parser = std::make_unique<FlagRefusingParser>();

    CHECK(dev.fujicore_set_query("/a", FN_QUERY_OUTPUT_ASCII).is_error());
    CHECK(dev.fujicore_set_query("/a", 0).is_success()); // no flags byte: nothing to refuse
}
