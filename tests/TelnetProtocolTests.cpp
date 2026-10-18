#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Telnet.h"
#include "samlib.h"

FakePeer fake_peer;

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

struct TelnetConnection
{
    std::string rx, tx, sp;
    NetworkProtocolTELNET telnet{&rx, &tx, &sp};

    TelnetConnection()
    {
        fake_peer = FakePeer();
        auto url = PeoplesUrlParser::parseURL("TELNET://peer:23/");
        REQUIRE(telnet.open(url.get(), ACCESS_MODE::READWRITE, NETPROTO_TRANS::NONE)
                == FUJI_ERROR::NONE);
    }
};

TEST_CASE("a TELNET read passes libtelnet only the bytes that arrived (#1916)")
{
    TelnetConnection t;
    fake_peer.inbound = "HELLO";
    CHECK(t.telnet.read(10) == FUJI_ERROR::NONE);
    CHECK(t.rx == "HELLO");

    t.rx.clear();
    CHECK(t.telnet.read(10) != FUJI_ERROR::NONE);
    CHECK(t.telnet.error == NDEV_STATUS::SOCKET_TIMEOUT);
    CHECK(t.rx.empty());
}

TEST_CASE("two TELNET channels keep separate libtelnet parsers (#1915)")
{
    TelnetConnection a;
    std::string rx2, tx2, sp2;
    NetworkProtocolTELNET second{&rx2, &tx2, &sp2};
    auto url = PeoplesUrlParser::parseURL("TELNET://peer:23/");
    REQUIRE(second.open(url.get(), ACCESS_MODE::READWRITE, NETPROTO_TRANS::NONE) == FUJI_ERROR::NONE);

    fake_peer.inbound = "HELLO";
    CHECK(a.telnet.read(5) == FUJI_ERROR::NONE);
    CHECK(a.rx == "HELLO");
    CHECK(rx2.empty());

    fake_peer.inbound = "WORLD";
    CHECK(second.read(5) == FUJI_ERROR::NONE);
    CHECK(rx2 == "WORLD");
    CHECK(a.rx == "HELLO");
}

TEST_CASE("a short TELNET send is an error and is not resent by the next WRITE (#1919)")
{
    TelnetConnection t;
    fake_peer.write_limit = 4;
    t.tx = "BBBBBBBBBB";
    CHECK(t.telnet.write(10) != FUJI_ERROR::NONE);
    CHECK(t.telnet.error == NDEV_STATUS::SOCKET_TIMEOUT);
    CHECK(fake_peer.received == "BBBB");

    fake_peer.write_limit = SIZE_MAX;
    t.tx = "Z";
    CHECK(t.telnet.write(1) == FUJI_ERROR::NONE);
    CHECK(fake_peer.received == "BBBBZ");

    t.tx = "A\xff" "A";
    CHECK(t.telnet.write(3) == FUJI_ERROR::NONE);
    CHECK(fake_peer.received == "BBBBZA\xff\xff" "A");
}
