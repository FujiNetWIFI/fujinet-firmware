#ifndef COMCHANNEL_H
#define COMCHANNEL_H

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"

#ifdef HELLO_IM_A_PC

#include <winsock2.h>
#include <windows.h>

struct ChannelConfig
{
    std::string device;
    int baud_rate;
    double read_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;
    double discard_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;

    ChannelConfig& baud(int baud) {
        baud_rate = baud; return *this;
    }
    ChannelConfig& deviceID(std::string path) {
        device = path; return *this;
    }
    ChannelConfig& readTimeout(double millis) {
        read_timeout_ms = millis; return *this;
    }
    ChannelConfig& discardTimeout(double millis) {
        discard_timeout_ms = millis; return *this;
    }
};

class COMChannel : public IOChannel, public RS232ChannelProtocol
{
private:
    HANDLE _fd = INVALID_HANDLE_VALUE;
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
    bool getDCD() override;           // DTE DCD input
    bool getRI() override;            // DTE RI input

    void setDCD(bool state) override { return; } // DCD is not an output on DTE
    void setRI(bool state) override { return; }  // RI is not an output on DTE
};

#endif /* HELLO_IM_A_PC */

#endif /* COMCHANNEL_H */
