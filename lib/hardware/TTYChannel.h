#ifndef TTYCHANNEL_H
#define TTYCHANNEL_H

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"

#ifdef ITS_A_UNIX_SYSTEM_I_KNOW_THIS

#define FN_UART_BUS ""

struct ChannelConfig
{
    std::string device;
    int baud_rate;
    uint32_t read_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;
    uint32_t discard_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;

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

class TTYChannel : public IOChannel, public RS232ChannelProtocol
{
private:
    int _fd = -1;
    std::string _device;
    uint32_t _baud;
    bool _dtrState = true, _rtsState = true;

protected:
    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin(const ChannelConfig& conf);
    void end() override;

    void flushOutput() override;

    uint32_t getBaudrate() override { return _baud; }
    void setBaudrate(uint32_t baud) override;

    // FujiNet acts as modem (DCE), computer serial ports are DTE.
    // API names follow the modem (DCE) view, but the actual RS-232 pin differs.
    bool getDTR() override;           // modem DTR input  → actually reads RS-232 DSR pin
    void setDSR(bool state) override; // modem DSR output → actually drives RS-232 DTR pin
    bool getRTS() override;           // modem RTS input  → actually reads RS-232 CTS pin
    void setCTS(bool state) override; // modem CTS output → actually drives RS-232 RTS pin
    bool getRI() override;            // Ring Indicator is only an input on DTE :-/

    void setPort(std::string device);
    std::string getPort();
};

#endif /* ITS_A_UNIX_SYSTEM_I_KNOW_THIS */

#endif /* TTYCHANNEL_H */
