#ifndef NETADAMNET_H
#define NETADAMNET_H

// AdamNet "Bus over IP" transport for fujinet-pc.
//
// On real hardware the AdamNet bus is a 62500-baud half-duplex one-wire serial
// line driven through a UARTChannel. For the PC ADAM target we instead carry the
// raw AdamNet wire bytes over a TCP stream to an emulator (AdamEm), which acts as
// the AdamNet master. fujinet-pc is the TCP *client*; the emulator listens.
//
// The one-wire bus echoes everything we transmit back into our own RX. A point-
// to-point TCP socket does not, so dataOut() also appends the transmitted bytes
// to our own FIFO ("local echo") -- this reproduces the half-duplex echo so the
// bus service's drain_echo()/wait_for_idle() logic works unchanged.

#ifndef ESP_PLATFORM // PC-only transport

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"
#include "compat_inet.h"

#include <cstdint>
#include <string>

class NetAdamNet : public IOChannel, public RS232ChannelProtocol
{
private:
    std::string _host;
    uint16_t _port = 0;
    int _fd = -1;            // connected stream socket (-1 = not connected)
    uint32_t _baud = 62500;
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
    void begin(const std::string &host, int port, int baud = 62500);
    void end() override;
    void flushOutput() override;

    bool connected() const { return _fd >= 0; }

    // Block up to ms milliseconds waiting for incoming data (or just nap if not
    // connected). Called when the bus is idle so the PC main loop doesn't spin
    // at 100% CPU. Returns as soon as a byte is readable.
    void poll(int ms);

    // RS232ChannelProtocol: there are no real control lines over the socket.
    uint32_t getBaudrate() override { return _baud; }
    void setBaudrate(uint32_t baud) override { _baud = baud; }
    bool getDTR() override { return true; }
    void setDSR(bool state) override { (void)state; }
    bool getRTS() override { return true; }
    void setCTS(bool state) override { (void)state; }
    bool getDCD() override { return true; }
    void setDCD(bool state) override { (void)state; }
    void setRI(bool state) override { (void)state; }
    bool getRI() override { return false; }
};

#endif // !ESP_PLATFORM

#endif // NETADAMNET_H
