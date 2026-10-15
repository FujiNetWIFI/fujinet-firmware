#ifndef TCP_HARNESS_FNTCPSERVER_H
#define TCP_HARNESS_FNTCPSERVER_H

// Test stand-in for lib/tcpip/fnTcpServer.h: listens, and hands out the caller
// queued in fake_peer.waiting.

#include "fnTcpClient.h"

class fnTcpServer
{
public:
    fnTcpServer(uint16_t = 80, uint8_t = 4) {}
    int begin(uint16_t = 0) { return 1; }
    bool hasClient() { return fake_peer.waiting; }

    fnTcpClient client()
    {
        fake_peer.waiting = false;
        fnTcpClient c;
        c.connect("", 0);
        return c;
    }

    void stop() {}
};

#endif
