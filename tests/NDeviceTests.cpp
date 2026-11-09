#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "NDevice/NDevice.h"
#include "NDevice/NParser.h"
#include "sioNetworkStubs.h"
#include "fn_query_flags.h"

// The shared NDevice, with no bus subclass on top.
class TestableNDevice : public NDevice
{
public:
    using NDevice::fujicore_set_query;
    using NDevice::fujicore_set_parser;
    using NDevice::fujicore_set_prefix;
    using NDevice::fujidev_write;
    using NDevice::_parser;
    using NDevice::_protocol;
    using NDevice::prefix;

    // The test build's virtualDevice is SIO's; these hooks are never called here.
    void sio_status(const FujiSIOPacket &) override {}
    void sio_process(const FujiSIOPacket &) override {}
};

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
    TestableNDevice dev;
    dev._parser = std::make_unique<FlagRefusingParser>();

    CHECK(dev.fujicore_set_query("/a", FN_QUERY_OUTPUT_ASCII).is_error());
    CHECK(dev.fujicore_set_query("/a", 0).is_success()); // no flags byte: nothing to refuse
}

// --------------------------------------------------------------------------------
// A WRITE of zero bytes, which DOS COPY sends to set a file's length.
// --------------------------------------------------------------------------------

// An open channel the zero-byte write never reaches.
class OpenProtocol : public NetworkProtocol
{
public:
    OpenProtocol() : NetworkProtocol(nullptr, nullptr, nullptr) {}
    size_t available() override { return 0; }
};

static void write_nothing(TestableNDevice &dev)
{
    FujiSIOPacket packet;
    packet.frame.comnd = CMD::NET_WRITE;
    packet.frame.aux1 = 0;
    packet.frame.aux2 = 0;

    bus = {};
    dev.fujidev_write(packet);
}

TEST_CASE("a WRITE of zero bytes on an open channel succeeds")
{
    TestableNDevice dev;
    dev._protocol = std::make_unique<OpenProtocol>();

    write_nothing(dev);

    CHECK(bus.success == 1);
    CHECK(bus.error == 0);
}

TEST_CASE("a WRITE of zero bytes with nothing open fails")
{
    TestableNDevice dev;

    write_nothing(dev);

    CHECK(bus.success == 0);
    CHECK(bus.error == 1);
}

// --------------------------------------------------------------------------------
// SET PREFIX, against the real URL and path helpers.
// --------------------------------------------------------------------------------

static std::string prefix_after(const std::string &start, const std::string &path)
{
    TestableNDevice dev;
    dev.prefix = start;
    CHECK(dev.fujicore_set_prefix(path).is_success());
    return dev.prefix;
}

TEST_CASE("an absolute path replaces the path and keeps the host (#2211)")
{
    CHECK(prefix_after("/a/", "/b") == "/b/");
    CHECK(prefix_after("TNFS://host/a/", "/b") == "TNFS://host/b/");
    CHECK(prefix_after("SMB://u:pw@host:445/share/d/", "/b") == "SMB://u:pw@host:445/b/");
    CHECK(prefix_after("SD:/dir/", "/b") == "SD:/b/");
}

TEST_CASE("a bare / goes back to the host")
{
    CHECK(prefix_after("TNFS://host/a/b/", "/") == "TNFS://host/");
    CHECK(prefix_after("/a/b/", "/") == "/");
}

TEST_CASE("a relative path is appended to the prefix")
{
    CHECK(prefix_after("TNFS://host/a/", "c") == "TNFS://host/a/c/");
    CHECK(prefix_after("/a/", "c") == "/a/c/");
    CHECK(prefix_after("", "c") == "c/");
    CHECK(prefix_after("TNFS://host/a/b/", "..") == "TNFS://host/a/");
}

TEST_CASE("the SpartaDOS < and > go to the parent and the host (#2265)")
{
    for (const char *parent : {"<", "</"})
    {
        CHECK(prefix_after("TNFS://host/a/b/", parent) == "TNFS://host/a/");
        CHECK(prefix_after("/a/b/", parent) == "/a/");
    }
    for (const char *host : {">", ">/"})
    {
        CHECK(prefix_after("TNFS://host/a/b/", host) == "TNFS://host/");
        CHECK(prefix_after("/a/b/", host) == "/");
    }
}

TEST_CASE("a full URL replaces the prefix")
{
    CHECK(prefix_after("TNFS://host/a/", "SMB://other/d") == "SMB://other/d/");
}

TEST_CASE("an empty path clears the prefix")
{
    CHECK(prefix_after("TNFS://host/a/", "") == "");
}

// --------------------------------------------------------------------------------
// The document parsers end values in the bus's EOL (#2334).
// --------------------------------------------------------------------------------

static std::string eol_given_to(parserMode_t mode)
{
    TestableNDevice dev;
    parser_eol.clear();
    REQUIRE(dev.fujicore_set_parser(mode).is_success());
    return parser_eol;
}

TEST_CASE("the JSON, HTML and XML parsers end values in the Atari's $9B (#2334)")
{
    CHECK(eol_given_to(PARSER::JSON) == "\x9b");
    CHECK(eol_given_to(PARSER::HTML) == "\x9b");
    CHECK(eol_given_to(PARSER::XML) == "\x9b");
}
