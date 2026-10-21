#include "adamNetwork.h"
#include "NParser.h"
#include "adamNetworkReceiveStubs.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <algorithm>

class BufferedParser : public NParser
{
    std::string data;

public:
    explicit BufferedParser(std::string bytes) : NParser(nullptr), data(std::move(bytes)) {}

    fujiError_t read(std::string &buffer, size_t length) override
    {
        const size_t count = std::min(length, data.size());
        buffer.assign(data, 0, count);
        data.erase(0, count);
        return FUJI_ERROR::NONE;
    }

    size_t available() override { return data.size(); }
    NetworkStatus status() override { return {}; }
};

class TestAdamNetwork : public adamNetwork
{
public:
    using adamNetwork::adamnet_control_receive;
    void setParser(std::unique_ptr<NParser> parser) { _parser = std::move(parser); }
};

TEST_CASE("AdamNet RECEIVE sends at most 1024 bytes and leaves the remainder")
{
    bus = {};
    TestAdamNetwork device;
    std::string staged(2500, 'x');
    device.setParser(std::make_unique<BufferedParser>(staged));

    device.adamnet_control_receive();
    CHECK(bus.sent.size() == 1024);

    device.adamnet_control_receive();
    CHECK(bus.sent.size() == 1024);
    CHECK(bus.sent == std::string(1024, 'x'));

    device.adamnet_control_receive();
    CHECK(bus.sent.size() == 452);
    CHECK(bus.sent == std::string(452, 'x'));
}
