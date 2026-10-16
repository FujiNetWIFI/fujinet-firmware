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
    using NDevice::_parser;

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
