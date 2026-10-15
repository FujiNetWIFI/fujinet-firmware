#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cerrno>

#include "TCP.h"
#include "Telnet.h"
#include "samlib.h"

FakePeer fake_peer;

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

struct Connection
{
    std::string rx, tx, sp;
    NetworkProtocolTCP tcp{&rx, &tx, &sp};

    Connection(const char *spec = "TCP://peer:6502/")
    {
        fake_peer = FakePeer();
        auto url = PeoplesUrlParser::parseURL(spec);
        REQUIRE(tcp.open(url.get(), ACCESS_MODE::READWRITE, NETPROTO_TRANS::NONE)
                == FUJI_ERROR::NONE);
    }
};

struct Listener : Connection
{
    Listener() : Connection("TCP://:6502/") {}
};

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

TEST_CASE("a second ACCEPT while a client is live is refused and leaves it alone (#1921)")
{
    Listener l;
    fake_peer.waiting = true;
    REQUIRE(l.tcp.accept_connection() == FUJI_ERROR::NONE);

    fake_peer.waiting = true;
    CHECK(l.tcp.accept_connection() != FUJI_ERROR::NONE);
    CHECK(l.tcp.error == NDEV_STATUS::CONNECTION_ALREADY_IN_PROGRESS);
    CHECK(fake_peer.waiting);

    l.tx = "hi";
    CHECK(l.tcp.write(2) == FUJI_ERROR::NONE);
    CHECK(fake_peer.received == "hi");
}

TEST_CASE("CLOSE CLIENT with no server is an error; with no client or a live one it is not (#1922)")
{
    Connection c;
    CHECK(c.tcp.close_client_connection() != FUJI_ERROR::NONE);
    CHECK(c.tcp.error == NDEV_STATUS::SERVER_NOT_RUNNING);

    Listener l;
    CHECK(l.tcp.close_client_connection() == FUJI_ERROR::NONE);
    CHECK(l.tcp.error == NDEV_STATUS::NOT_CONNECTED);

    fake_peer.waiting = true;
    REQUIRE(l.tcp.accept_connection() == FUJI_ERROR::NONE);
    CHECK(l.tcp.close_client_connection() == FUJI_ERROR::NONE);
    l.tx = "x";
    CHECK(l.tcp.write(1) != FUJI_ERROR::NONE);
    CHECK(l.tcp.error == NDEV_STATUS::NOT_CONNECTED);
}

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

TEST_CASE("a short write is an error and is not resent by the next WRITE (#1918)")
{
    Connection c;
    fake_peer.write_limit = 4;
    c.tx = "BBBBBBBBBB";
    CHECK(c.tcp.write(10) != FUJI_ERROR::NONE);
    CHECK(fake_peer.received == "BBBB");

    fake_peer.write_limit = SIZE_MAX;
    c.tx += "Z";
    CHECK(c.tcp.write(1) == FUJI_ERROR::NONE);
    CHECK(fake_peer.received == "BBBBZ");
}

TEST_CASE("a stale ECONNRESET in errno does not fail a good read or write (#1920)")
{
    Connection c;
    fake_peer.inbound = "HELLO";
    errno = ECONNRESET;
    CHECK(c.tcp.read(5) == FUJI_ERROR::NONE);
    CHECK(c.rx == "HELLO");

    c.tx = "Z";
    errno = ECONNRESET;
    CHECK(c.tcp.write(1) == FUJI_ERROR::NONE);
    CHECK(fake_peer.received == "Z");
}

TEST_CASE("a write cut short by a reset reports CONNECTION_RESET, a stalled one SOCKET_TIMEOUT (#1920)")
{
    Connection c;
    fake_peer.write_limit = 1;
    c.tx = "BB";
    CHECK(c.tcp.write(2) != FUJI_ERROR::NONE);
    CHECK(c.tcp.error == NDEV_STATUS::SOCKET_TIMEOUT);

    fake_peer.write_limit = SIZE_MAX;
    fake_peer.reset_on_write = true;
    c.tx = "B";
    CHECK(c.tcp.write(1) != FUJI_ERROR::NONE);
    CHECK(c.tcp.error == NDEV_STATUS::CONNECTION_RESET);
}
