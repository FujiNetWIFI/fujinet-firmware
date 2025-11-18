#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "bus/rs232/FujiBusPacket.h"

static ByteBuffer corrupt_one_byte(ByteBuffer buf)
{
    if (buf.size() > 5)
        buf[5] ^= 0xFF;
    return buf;
}

static FujiBusPacket make_reference_packet()
{
    auto dev = static_cast<fujiDeviceID_t>(42);
    auto cmd = static_cast<fujiCommandID_t>(99);

    // 3 params of different sizes + no payload
    return FujiBusPacket(dev, cmd,
                         std::uint8_t{0x11},
                         std::uint16_t{0x2233},
                         std::uint32_t{0x44556677});
}

// Compares "interesting" user-visible fields of a packet
static void check_packets_equal(const FujiBusPacket& a, const FujiBusPacket& b)
{
    CHECK(a.device()     == b.device());
    CHECK(a.command()    == b.command());
    CHECK(a.paramCount() == b.paramCount());

    for (unsigned i = 0; i < a.paramCount(); ++i)
        CHECK(a.param(i) == b.param(i));

    auto da = a.data();
    auto db = b.data();

    CHECK(da.has_value() == db.has_value());
    if (da && db)
    {
        // compare as bytes via string_data_or("") or similar if you’ve added that helper
        std::string sa(da->begin(), da->end());
        std::string sb(db->begin(), db->end());
        CHECK(sa == sb);
    }
}

static void check_slip_framed(const ByteBuffer& bytes)
{
    REQUIRE(!bytes.empty());
    CHECK(bytes.front() == SLIP_END);
    CHECK(bytes.back()  == SLIP_END);
}

// --------------------------------------------------------------------------------
// TEST CASES
// --------------------------------------------------------------------------------

TEST_CASE("serialize() produces a SLIP-framed packet")
{
    // Use some arbitrary device/command IDs:
    auto dev = static_cast<fujiDeviceID_t>(1);
    auto cmd = static_cast<fujiCommandID_t>(2);

    FujiBusPacket pkt(dev, cmd, std::uint8_t{0x12}, std::uint16_t{0x3456});
    ByteBuffer serialized = pkt.serialize();

    check_slip_framed(serialized);
}

TEST_CASE("simple roundtrip: no payload, a few params")
{
    auto dev = static_cast<fujiDeviceID_t>(1);
    auto cmd = static_cast<fujiCommandID_t>(2);

    FujiBusPacket pkt(dev, cmd,
                      std::uint8_t{0x11},
                      std::uint16_t{0x2233},
                      std::uint32_t{0x44556677});

    ByteBuffer serialized = pkt.serialize();

    // SLIP framing
    check_slip_framed(serialized);

    auto parsed = FujiBusPacket::fromSerialized(serialized);
    REQUIRE(parsed);

    CHECK(parsed->device()     == dev);
    CHECK(parsed->command()    == cmd);
    CHECK(parsed->paramCount() == 3);
    CHECK(parsed->param(0)     == 0x11);
    CHECK(parsed->param(1)     == 0x2233);
    CHECK(parsed->param(2)     == 0x44556677u);
    CHECK_FALSE(parsed->data().has_value());
}

TEST_CASE("roundtrip with binary payload (includes SLIP specials)")
{
    auto dev = static_cast<fujiDeviceID_t>(3);
    auto cmd = static_cast<fujiCommandID_t>(4);

    ByteBuffer payload{0x00, 0xC0, 0xDB, 0xFF}; // includes SLIP_END and SLIP_ESCAPE

    FujiBusPacket pkt(dev, cmd,
                      std::uint8_t{0xAA},
                      payload);

    ByteBuffer serialized = pkt.serialize();

    // still SLIP framed even with special bytes inside
    check_slip_framed(serialized);

    auto parsed = FujiBusPacket::fromSerialized(serialized);
    REQUIRE(parsed);

    CHECK(parsed->device()     == dev);
    CHECK(parsed->command()    == cmd);
    CHECK(parsed->paramCount() == 1);
    CHECK(parsed->param(0)     == 0xAA);

    REQUIRE(parsed->data().has_value());
    const ByteBuffer& back = *parsed->data();
    REQUIRE(back.size() == payload.size());
    CHECK(std::equal(back.begin(), back.end(), payload.begin()));
}

TEST_CASE("roundtrip with textual payload via std::string")
{
    auto dev = static_cast<fujiDeviceID_t>(5);
    auto cmd = static_cast<fujiCommandID_t>(6);

    std::string tz = "Europe/London";

    FujiBusPacket pkt(dev, cmd,
                      std::uint16_t{0x1234},
                      tz); // uses string overload

    ByteBuffer serialized = pkt.serialize();

    auto parsed = FujiBusPacket::fromSerialized(serialized);
    REQUIRE(parsed);

    CHECK(parsed->device()     == dev);
    CHECK(parsed->command()    == cmd);
    CHECK(parsed->paramCount() == 1);
    CHECK(parsed->param(0)     == 0x1234);

    auto tz_opt = parsed->data_as_string();
    REQUIRE(tz_opt.has_value());
    CHECK(tz_opt.value() == tz);
}

TEST_CASE("checksum detects corruption")
{
    auto dev = static_cast<fujiDeviceID_t>(7);
    auto cmd = static_cast<fujiCommandID_t>(8);

    FujiBusPacket pkt(dev, cmd,
                      std::uint8_t{0x10},
                      std::uint8_t{0x20},
                      ByteBuffer{0x01, 0x02, 0x03, 0x04});

    ByteBuffer serialized = pkt.serialize();

    auto ok = FujiBusPacket::fromSerialized(serialized);
    REQUIRE(ok);

    ByteBuffer bad = corrupt_one_byte(serialized);
    auto broken = FujiBusPacket::fromSerialized(bad);
    CHECK(broken == nullptr); // parse should fail due to checksum mismatch
}

TEST_CASE("multiple descriptors: many u8 followed by u16")
{
    auto dev = static_cast<fujiDeviceID_t>(9);
    auto cmd = static_cast<fujiCommandID_t>(10);

    // 5 uint8_t params forces:
    // - first descriptor: 4×u8
    // - second descriptor: 1×u8
    FujiBusPacket pkt(dev, cmd,
                      std::uint8_t{1},
                      std::uint8_t{2},
                      std::uint8_t{3},
                      std::uint8_t{4},
                      std::uint8_t{5},
                      std::uint16_t{0xABCD});

    ByteBuffer serialized = pkt.serialize();

    auto parsed = FujiBusPacket::fromSerialized(serialized);
    REQUIRE(parsed);

    CHECK(parsed->device()     == dev);
    CHECK(parsed->command()    == cmd);
    CHECK(parsed->paramCount() == 6);

    CHECK(parsed->param(0) == 1);
    CHECK(parsed->param(1) == 2);
    CHECK(parsed->param(2) == 3);
    CHECK(parsed->param(3) == 4);
    CHECK(parsed->param(4) == 5);
    CHECK(parsed->param(5) == 0xABCD);
}

TEST_CASE("parser skips junk before first SLIP_END")
{
    FujiBusPacket refPkt = make_reference_packet();
    ByteBuffer clean = refPkt.serialize();

    // Prepend a bunch of garbage bytes before the valid SLIP frame
    ByteBuffer noisy;
    noisy.push_back(0x00);
    noisy.push_back(0xAA);
    noisy.push_back(0x55);
    noisy.push_back(0xFF);
    noisy.insert(noisy.end(), clean.begin(), clean.end());

    // Parsing the noisy buffer should still succeed and match the reference packet
    auto parsed = FujiBusPacket::fromSerialized(noisy);
    REQUIRE(parsed);

    check_packets_equal(refPkt, *parsed);
}

TEST_CASE("invalid: no SLIP_END at all")
{
    ByteBuffer buf{0x01, 0x02, 0x03, 0x04, 0x05}; // totally bogus

    auto parsed = FujiBusPacket::fromSerialized(buf);
    CHECK(parsed == nullptr);
}

TEST_CASE("invalid: SLIP framed but decoded data too short for header")
{
    // After SLIP decode, we'll just get the middle bytes.
    ByteBuffer buf{SLIP_END, 0x01, 0x02, SLIP_END};

    auto parsed = FujiBusPacket::fromSerialized(buf);
    CHECK(parsed == nullptr);
}

TEST_CASE("invalid: SLIP frame with only END markers")
{
    ByteBuffer buf{SLIP_END, SLIP_END}; // decodes to empty payload

    auto parsed = FujiBusPacket::fromSerialized(buf);
    CHECK(parsed == nullptr);
}

TEST_CASE("invalid: missing trailing SLIP_END")
{
    FujiBusPacket pkt = make_reference_packet();
    ByteBuffer serialized = pkt.serialize();

    REQUIRE(serialized.size() >= 2);
    serialized.pop_back(); // drop last SLIP_END

    auto parsed = FujiBusPacket::fromSerialized(serialized);
    CHECK(parsed == nullptr);
}

TEST_CASE("invalid: missing leading SLIP_END and no other SLIP_END")
{
    FujiBusPacket pkt = make_reference_packet();
    ByteBuffer serialized = pkt.serialize();

    // serialized should be: END ... END
    check_slip_framed(serialized);

    // Drop the first SLIP_END and *also* the last so there are none
    serialized.erase(serialized.begin());   // remove first END
    serialized.pop_back();                  // remove last END

    auto parsed = FujiBusPacket::fromSerialized(serialized);
    CHECK(parsed == nullptr);
}
