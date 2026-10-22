#include "tnfslib_tcp_stream.h"

success_is_true tnfsTcpStream::open(const char *hostname, in_addr_t &address, uint16_t port,
                                    int timeout_ms)
{
    if (address != IPADDR_NONE)
        RETURN_SUCCESS_IF(_client.connect(address, port, timeout_ms));

    bool connected = _client.connect(hostname, port, timeout_ms);
    address = _client.remoteIP();
    RETURN_SUCCESS_IF(connected);
}

bool tnfsTcpStream::is_open() { return _client.connected(); }

size_t tnfsTcpStream::available() { return _client.available(); }

int tnfsTcpStream::read(uint8_t *buf, size_t size) { return _client.read(buf, size); }

size_t tnfsTcpStream::write(const uint8_t *buf, size_t size)
{
    return _client.write(buf, size);
}

void tnfsTcpStream::close() { _client.stop(); }
