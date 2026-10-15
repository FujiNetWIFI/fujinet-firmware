#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "TCP.h"
#include "samlib.h"

FakePeer fake_peer;

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

struct Connection
{
    std::string rx, tx, sp;
    NetworkProtocolTCP tcp{&rx, &tx, &sp};

    Connection()
    {
        fake_peer = FakePeer();
        auto url = PeoplesUrlParser::parseURL("TCP://peer:6502/");
        REQUIRE(tcp.open(url.get(), ACCESS_MODE::READWRITE, NETPROTO_TRANS::NONE)
                == FUJI_ERROR::NONE);
    }
};

TEST_CASE("a read shorter than asked keeps what arrived (#1917)")
{
    Connection c;
    fake_peer.inbound = "HELLO";
    CHECK(c.tcp.read(10) == FUJI_ERROR::NONE);
    CHECK(c.rx == "HELLO");
    CHECK(fake_peer.inbound.empty());
}

TEST_CASE("a read of exactly what arrived returns it")
{
    Connection c;
    fake_peer.inbound = "HELLO";
    CHECK(c.tcp.read(5) == FUJI_ERROR::NONE);
    CHECK(c.rx == "HELLO");
}

TEST_CASE("a read with nothing waiting is an error and adds nothing")
{
    Connection c;
    CHECK(c.tcp.read(10) != FUJI_ERROR::NONE);
    CHECK(c.rx.empty());
}
