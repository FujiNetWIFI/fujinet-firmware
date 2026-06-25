// Minimal stub of fnUDP for the standalone TNFS harness. The harness forces the
// TCP path, so these methods are compiled (a fnUDP object is constructed per
// transaction) but never actually exercised; bodies are no-ops.
#ifndef _STUB_FNUDP_H
#define _STUB_FNUDP_H

#include <cstdint>
#include <cstddef>
#include <netinet/in.h> // in_addr_t

class fnUDP
{
public:
    fnUDP() {}
    ~fnUDP() {}

    bool beginPacket() { return false; }
    bool beginPacket(in_addr_t, uint16_t) { return false; }
    bool beginPacket(const char *, uint16_t) { return false; }
    bool endPacket() { return false; }

    size_t write(uint8_t) { return 0; }
    size_t write(const uint8_t *, size_t) { return 0; }

    int parsePacket() { return 0; }
    int read() { return -1; }
    int read(unsigned char *, size_t) { return -1; }
    int read(char *, size_t) { return -1; }

    in_addr_t remoteIP() { return INADDR_NONE; }
};
#endif
