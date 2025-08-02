#ifndef TTYCHANNEL_H
#define TTYCHANNEL_H

#include "IOChannel.h"

#ifdef ITS_A_UNIX_SYSTEM_I_KNOW_THIS

#define FN_UART_BUS ""

struct ChannelConfig
{
    std::string device;
    int baud_rate;
    uint32_t read_timeout_ms = 10;
    uint32_t discard_timeout_ms = 10;

    ChannelConfig& baud(int baud) {
        baud_rate = baud; return *this;
    }
    ChannelConfig& deviceID(std::string path) {
        device = path; return *this;
    }
    ChannelConfig& readTimeout(uint32_t millis) {
        read_timeout_ms = millis; return *this;
    }
    ChannelConfig& discardTimeout(uint32_t millis) {
        discard_timeout_ms = millis; return *this;
    }
};

class TTYChannel : public IOChannel
{
private:
    int _fd;
    std::string _device;
    uint32_t _baud;
    uint32_t read_timeout_ms = 10;
    uint32_t discard_timeout_ms = 10;

protected:
    void update_fifo() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin(const ChannelConfig& conf);
    void end() override;

    void flush() override;

    uint32_t getBaudrate() override { return _baud; }
    void setBaudrate(uint32_t baud) override;

#ifdef UNUSED
    size_t available() override;
    size_t si_recv(void *buffer, size_t length) override;
    bool waitReadable(uint32_t timeout_ms);
#endif /* UNUSED */

    bool dtrState();
    void setPort(std::string device);
    std::string getPort();

};

#endif /* ITS_A_UNIX_SYSTEM_I_KNOW_THIS */

#endif /* SERIALUART_H */
