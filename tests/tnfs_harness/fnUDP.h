#ifndef TNFS_HARNESS_FNUDP_H
#define TNFS_HARNESS_FNUDP_H

// Test stand-in for lib/tcpip/fnUDP.h: datagrams to and from harness_server.

#include <cstddef>
#include <cstdint>

#include "compat_inet.h"
#include "harness_server.h"

class fnUDP
{
public:
    bool beginPacket(in_addr_t, uint16_t) { return _out.clear(), true; }
    bool beginPacket(const char *, uint16_t) { return _out.clear(), true; }
    size_t write(const uint8_t *buf, size_t size)
    {
        _out.insert(_out.end(), buf, buf + size);
        return size;
    }
    bool endPacket()
    {
        harness_server.udp_requests++;
        harness_server.on_request(_out);
        return true;
    }
    int parsePacket() { return static_cast<int>(harness_server.datagram_ready()); }
    int read(unsigned char *buf, size_t size)
    { return static_cast<int>(harness_server.read_datagram(buf, size)); }
    in_addr_t remoteIP() { return 0x0100007F; }

private:
    HarnessServer::Bytes _out;
};

#endif
