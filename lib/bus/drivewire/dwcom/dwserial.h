#ifndef DWSERIAL_H
#define DWSERIAL_H

#include <stdio.h>

#include "SerialUART.h"
#include "dwport.h"

/*
 * Implementation of DriveWire Port using UART serial port
 * wrapper around existing SerialUART
 */

class SerialDwPort : public DwPort
{
private:
    SerialUART _uart;

public:
    SerialDwPort() {}
    virtual void begin(int baud) override;
    virtual void end() override { _uart.end(); }
    virtual bool poll(int ms) override 
    {
#ifdef ESP_PLATFORM
        return false;
#else
        return _uart.poll(ms); 
#endif
    }

    virtual int available() override { return _uart.available(); }
    virtual void flush() override { _uart.flush(); }
    virtual void flush_input() override { _uart.discardInput(); }

    // read bytes into buffer
    virtual size_t read(uint8_t *buffer, size_t size) override;
    // write buffer
    virtual ssize_t write(const uint8_t *buffer, size_t size) override;

    // specific to SerialDwPort/UART
    void set_baudrate(uint32_t baud) override { _uart.setBaudrate(baud); }
    uint32_t get_baudrate() override { return _uart.getBaudrate(); }
#ifndef ESP_PLATFORM
    void set_port(const char *device) { _uart.set_port(device); }
    const char* get_port() { return _uart.get_port(); }
#endif
};

#endif // DWSERIAL_H
