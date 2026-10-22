#ifndef _TNFSLIB_TCP_STREAM_H
#define _TNFSLIB_TCP_STREAM_H

// TNFS over TCP: tnfsStreamTransport on fnTcpClient.

#include "fnTcpClient.h"
#include "tnfslib_stream_transport.h"

class tnfsTcpStream : public tnfsStreamTransport
{
public:
    success_is_true open(const char *hostname, in_addr_t &address, uint16_t port,
                         int timeout_ms) override;
    bool is_open() override;
    size_t available() override;
    int read(uint8_t *buf, size_t size) override;
    size_t write(const uint8_t *buf, size_t size) override;
    void close() override;

private:
    fnTcpClient _client;
};

#endif // _TNFSLIB_TCP_STREAM_H
