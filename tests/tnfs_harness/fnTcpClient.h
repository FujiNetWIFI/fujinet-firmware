#ifndef TNFS_HARNESS_FNTCPCLIENT_H
#define TNFS_HARNESS_FNTCPCLIENT_H

// Test stand-in for lib/tcpip/fnTcpClient.h: a connection to harness_server.

#include <cstddef>
#include <cstdint>

#include "compat_inet.h"
#include "harness_server.h"

class fnTcpClient
{
public:
    int connect(in_addr_t, uint16_t, int32_t = -1) { return open(); }
    int connect(const char *, uint16_t, int32_t = -1) { return open(); }

    // Closing the socket loses whatever the peer sent and was not read.
    void stop()
    {
        if (_connected)
            harness_server.discard();
        _connected = false;
    }

    uint8_t connected()
    {
        if (_connected && harness_server.tcp_closing && harness_server.drained())
            _connected = false;
        return _connected;
    }

    size_t write(const uint8_t *buf, size_t size)
    {
        if (!connected())
            return 0;
        harness_server.on_request(HarnessServer::Bytes(buf, buf + size));
        return size;
    }

    size_t available() { return _connected ? harness_server.stream_ready() : 0; }
    int read(uint8_t *buf, size_t size)
    { return _connected ? static_cast<int>(harness_server.read_stream(buf, size)) : 0; }

    in_addr_t remoteIP() const { return 0x0100007F; }

private:
    int open()
    {
        harness_server.tcp_connects++;
        if (!harness_server.tcp_accepts)
            return 0;
        harness_server.discard();
        harness_server.tcp_closing = false;
        _connected = true;
        return 1;
    }

    bool _connected = false;
};

#endif
