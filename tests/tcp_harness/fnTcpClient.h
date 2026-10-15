#ifndef TCP_HARNESS_FNTCPCLIENT_H
#define TCP_HARNESS_FNTCPCLIENT_H

// Test stand-in for lib/tcpip/fnTcpClient.h: one connection to fake_peer.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

#include "compat_inet.h"

struct FakePeer
{
    std::string inbound;            // sent by the peer and not read yet
    std::string received;           // everything the client wrote
    size_t write_limit = SIZE_MAX;  // most bytes one write() sends
    bool waiting = false;           // a caller is queued on the listener
};

extern FakePeer fake_peer;

class fnTcpClient
{
public:
    int connect(const char *, uint16_t, int32_t = -1) { return _connected = true; }
    int connect(in_addr_t, uint16_t, int32_t = -1) { return _connected = true; }
    void stop() { _connected = false; }
    uint8_t connected() { return _connected; }
    size_t available() { return _connected ? fake_peer.inbound.size() : 0; }

    // TCP.cpp reads errno after these.
    int read(uint8_t *buf, size_t size)
    {
        errno = 0;
        size_t n = std::min(available(), size);
        memcpy(buf, fake_peer.inbound.data(), n);
        fake_peer.inbound.erase(0, n);
        return static_cast<int>(n);
    }

    size_t write(const uint8_t *buf, size_t size)
    {
        errno = 0;
        if (!_connected)
            return 0;
        size_t n = std::min(size, fake_peer.write_limit);
        fake_peer.received.append(reinterpret_cast<const char *>(buf), n);
        return n;
    }

    in_addr_t remoteIP() const { return 0x0100007F; }
    uint16_t remotePort() const { return 0; }

private:
    bool _connected = false;
};

#endif
