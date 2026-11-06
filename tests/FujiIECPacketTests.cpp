#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "bus/iec/FujiIECPacket.h"

static constexpr uint8_t DEV_FUJI = 30;

TEST_CASE("data() and dataAsString() are empty for a command sent with no payload")
{
    // fujinet-lib sends WRITE APPKEY for an empty key as OPCODE_NO_PAYLOAD
    FujiIECPacket packet(DEV_FUJI, CMD::FUJI_WRITE_APPKEY);

    CHECK_FALSE(packet.data().has_value());
    CHECK(packet.data().value_or(ByteBuffer{}).empty());
    CHECK_FALSE(packet.dataAsString().has_value());
}

TEST_CASE("data() holds what is left after the params")
{
    ByteBuffer payload{0x05, 0x01, 'k', 'e', 'y'};
    FujiIECPacket packet(DEV_FUJI, CMD::FUJI_OPEN_APPKEY, payload);

    CHECK(packet.param8(0) == 0x05);
    CHECK(packet.param8(1) == 0x01);
    REQUIRE(packet.data().has_value());
    CHECK(packet.data()->size() == 3);
    CHECK(packet.dataAsString().value_or("") == "key");
}

TEST_CASE("a param the command was sent without reads as 0")
{
    // fujinet-lib sends STATUS and GETTZ_LEN as OPCODE_NO_PAYLOAD
    FujiIECPacket bare(DEV_FUJI, CMD::FUJI_STATUS);

    CHECK_FALSE(bare.hasParam(0));
    CHECK(bare.param8(0) == 0);
    CHECK_FALSE(bare.data().has_value());

    // One byte is too short for a 16-bit param, and is left as data
    ByteBuffer payload{0x05};
    FujiIECPacket shortPacket(DEV_FUJI, CMD::APETIME_SETTZ_ALT, payload);

    CHECK_FALSE(shortPacket.hasParam(0, 2));
    CHECK(shortPacket.param16(0) == 0);
    REQUIRE(shortPacket.data().has_value());
    CHECK(shortPacket.data()->size() == 1);
}

TEST_CASE("hasParam() tells which params the command was sent with")
{
    ByteBuffer payload{0x01};
    FujiIECPacket packet(DEV_FUJI, CMD::APETIME_GET_SOS, payload);

    CHECK(packet.hasParam(0));
    CHECK_FALSE(packet.hasParam(1));
    CHECK(packet.param8(0) == 0x01);
    CHECK(packet.hasParam(0));
    CHECK_FALSE(packet.hasParam(1));
    CHECK(packet.param8(1) == 0);
}

TEST_CASE("params are little-endian")
{
    ByteBuffer payload{0x1C, 0xE4, 0x05, 0x00};
    FujiIECPacket packet(DEV_FUJI, CMD::FUJI_OPEN_APPKEY, payload);

    CHECK(packet.param16(0) == 0xE41C);
    CHECK(packet.param16(1) == 0x0005);
    REQUIRE(packet.data().has_value());
    CHECK(packet.data()->empty());
}
