#ifndef TCP_HARNESS_FNTCPSERVER_H
#define TCP_HARNESS_FNTCPSERVER_H

// Test stand-in for lib/tcpip/fnTcpServer.h: listens, never has a client.

#include "fnTcpClient.h"

class fnTcpServer
{
public:
    fnTcpServer(uint16_t = 80, uint8_t = 4) {}
    int begin(uint16_t = 0) { return 1; }
    bool hasClient() { return false; }
    fnTcpClient client() { return fnTcpClient(); }
    void stop() {}
};

#endif
