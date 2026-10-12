#ifndef PPICHANNEL_H
#define PPICHANNEL_H

#ifdef ESP_PLATFORM
#include "pinmap.h"
#endif /* ESP_PLATFORM */

#ifdef PIN_PPI_OBF

#include "IOChannel.h"
#include "RS232ChannelProtocol.h"

#include <freertos/FreeRTOS.h>

// Byte pipe to the host through an 82C55 port A in Mode 2 and a 74LVC245. The host
// owns the 245: DIR=1 passes host->ESP32, /OE low enables it.
class PPIChannel : public IOChannel, public RS232ChannelProtocol
{
private:
    uint32_t _dataMask0 = 0, _dataMask1 = 0; // D0-D7 in GPIO banks 0-31 and 32-39
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    uint64_t _lastWarn = 0;

    void releaseData();
    void driveData(uint8_t c);
    uint8_t sampleData();
    void warn(const char *msg);

protected:
    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t length) override;

public:
    void begin();
    void end() override;
    void flushOutput() override {}

    uint32_t getBaudrate() override { return 0; }
    void setBaudrate(uint32_t baud) override {}

    // No modem control lines on the 82C55 card
    bool getDTR() override { return true; }
    void setDSR(bool state) override {}
    bool getRTS() override { return true; }
    void setCTS(bool state) override {}
    void setDCD(bool state) override {}
    bool getDCD() override { return false; }
    void setRI(bool state) override {}
    bool getRI() override { return false; }
};

#endif /* PIN_PPI_OBF */

#endif /* PPICHANNEL_H */
