#ifndef TCP_HARNESS_FNUDP_H
#define TCP_HARNESS_FNUDP_H

// Test stand-in for lib/tcpip/fnUDP.h: the socket holds one fake datagram.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "compat_inet.h"

extern std::string fake_datagram;

class fnUDP
{
public:
    bool begin(uint16_t) { return true; }
    void stop() {}

    int available() { return fake_datagram.size(); }
    int read(uint8_t *buf, size_t len)
    {
        size_t got = std::min(len, fake_datagram.size());
        memcpy(buf, fake_datagram.data(), got);
        fake_datagram.erase(0, got);
        return got;
    }

    bool beginPacket(const char *, uint16_t) { return true; }
    size_t write(const uint8_t *, size_t len) { return len; }
    bool endPacket() { return true; }

    in_addr_t remoteIP() { return IPADDR_NONE; }
    uint16_t remotePort() { return 0; }
};

#endif // TCP_HARNESS_FNUDP_H
