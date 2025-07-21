#ifndef SERIALTTY_H
#define SERIALTTY_H

#include "SerialInterface.h"

#ifdef ITS_A_UNIX_SYSTEM_I_KNOW_THIS

#define FN_UART_BUS ""

struct SerialConfig
{
    std::string device;
    int baud_rate;

    SerialConfig& baud(int baud) {
        baud_rate = baud; return *this;
    }
    SerialConfig& deviceID(std::string path) {
        device = path; return *this;
    }
};

class SerialTTY : public SerialInterface
{
private:
    int _fd;
    std::string _device;
    uint32_t _baud;

protected:
    void update_fifo() override;
    size_t si_send(const void *buffer, size_t length) override;
    
public:
    void begin(const SerialConfig& conf);
    void end() override;

    void flush() override;
    
    uint32_t getBaudrate() override { return _baud; }
    void setBaudrate(uint32_t baud) override;

    bool dtrState();
    void setPort(std::string device);
    std::string getPort();
};

#endif /* ITS_A_UNIX_SYSTEM_I_KNOW_THIS */

#endif /* SERIALUART_H */
