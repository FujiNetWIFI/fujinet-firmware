#ifndef NETADAMNET_H
#define NETADAMNET_H

// AdamNet "Bus over IP" transport for fujinet-pc: carries raw AdamNet wire bytes
// over a TCP stream to an emulator (the master); fujinet-pc is the client.
// dataOut() also feeds TX back into our own RX ("local echo") to reproduce the
// one-wire half-duplex echo, so the bus's drain_echo()/wait_for_idle() work.

#ifndef ESP_PLATFORM // PC-only transport

#include "IOChannel.h"
#include "compat_inet.h"

#include <cstdint>
#include <string>

// No RS-232 control lines on the wire, so this does not implement RS232ChannelProtocol.
class NetAdamNet : public IOChannel
{
private:
    std::string _host;
    uint16_t _port = 0;
    int _fd = -1;            // connected stream socket (-1 = not connected)
    uint64_t _last_connect_attempt = 0;
    in_addr_t _ip = IPADDR_NONE; // resolved host, cached so we don't re-resolve
    bool _connect_warned = false; // logged the "can't reach" notice for this offline period

    // Try (re)connecting to the emulator. Throttled; safe to call often.
    bool ensure_connected();

protected:
    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    NetAdamNet() = default;
    ~NetAdamNet(); // IOChannel has no virtual dtor; held by value, never deleted via base

    // Connect (lazily/retrying) to the AdamNet master at host:port.
    void begin(const std::string &host, int port);
    void end() override;
    void flushOutput() override;

    bool connected() const { return _fd >= 0; }

    // Block up to ms for incoming data (naps if not connected); called when idle.
    void poll(int ms);
};

#endif // !ESP_PLATFORM

#endif // NETADAMNET_H
