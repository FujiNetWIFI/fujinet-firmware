#ifndef I8255CHANNEL_H
#define I8255CHANNEL_H

#include "IOChannel.h"

class i8255Channel : public IOChannel
{
private:
    int bus_available();
    int port_getc();
    int port_getc_timeout(uint16_t t);
    uint16_t port_getbuf(void *buf, uint16_t len, uint16_t timeout);
    int port_putc(uint8_t c);
    uint16_t port_putbuf(const void *buf, uint16_t len);

protected:
    size_t dataOut(const void *buffer, size_t length) override;
    void updateFIFO() override;

public:
    void begin();
    void end() override;

    void flushOutput() override;
};

#endif /* I8255CHANNEL_H */
