#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "UDP.h"
#include "fnTcpClient.h"
#include "fnDNS.h"
#include "samlib.h"

std::string fake_datagram;
FakePeer fake_peer; // TCP.cpp, which Protocol.cpp casts to, needs one

// Neither is called here; the stubs keep DNS and SAM out of the link.
in_addr_t get_ip4_addr_by_name(const char *hostname) { return IPADDR_NONE; }
int sam(int argc, char **argv) { return 0; }

struct Channel
{
    std::string rx, tx, sp;
    NetworkProtocolUDP udp{&rx, &tx, &sp};

    Channel()
    {
        fake_datagram.clear();
        PeoplesUrlParser url("UDP://peer:6502/");
        REQUIRE(udp.open(&url, ACCESS_MODE::READWRITE, NETPROTO_TRANS::NONE)
                == FUJI_ERROR::NONE);
    }
};

TEST_CASE("a datagram still on the socket is available")
{
    Channel chan;
    fake_datagram = "HELLO";
    CHECK(chan.udp.available() == 5);
}

TEST_CASE("a datagram a status poll moved to the buffer is still available (#2270)")
{
    Channel chan;
    fake_datagram = "HELLO";
    NetworkStatus status;
    CHECK(chan.udp.status(&status) == FUJI_ERROR::NONE);
    CHECK(chan.rx == "HELLO");
    CHECK(fake_datagram.empty());
    CHECK(chan.udp.available() == 5);
}

TEST_CASE("nothing waiting is not available")
{
    Channel chan;
    CHECK(chan.udp.available() == 0);
}
