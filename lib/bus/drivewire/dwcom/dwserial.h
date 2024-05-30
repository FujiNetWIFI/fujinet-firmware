#ifndef DWSERIAL_H
#define DWSERIAL_H

#include <stdio.h>

#include "fnUART.h"
#include "dwport.h"

/*
 * Implementation of DriveWire Port using UART serial port
 * wrapper around existing UARTManager
 */

class SerialDwPort : public DwPort
{
private:
    UARTManager _uart;
public:
    SerialDwPort() {}
    virtual void begin(int baud) override { _uart.begin(baud); }
    virtual void end() override { _uart.end(); }
    virtual bool poll(int ms) override 
    {
#ifdef ESP_PLATFORM
        return false;
#else
        return _uart.poll(ms); 
#endif
    }

    virtual void set_baudrate(uint32_t baud) override { _uart.set_baudrate(baud); }
    virtual uint32_t get_baudrate() override { return _uart.get_baudrate(); }

    virtual int available() override { return _uart.available(); }
    virtual void flush() override { _uart.flush(); }
    virtual void flush_input() override { _uart.flush_input(); }

    // read single byte
    virtual int read() override { return _uart.read(); }
    // read bytes into buffer
    virtual size_t read(uint8_t *buffer, size_t length) override {
        return _uart.readBytes(buffer, length);
    }

    // write single byte
    virtual ssize_t write(uint8_t b) override { return _uart.write(b); }
    // write buffer
    virtual ssize_t write(const uint8_t *buffer, size_t size) override {
        return _uart.write(buffer, size);
    }

    // specific to SerialDwPort/UART
#ifndef ESP_PLATFORM
    void set_port(const char *device) {
        _uart.set_port(device);
    }
    const char* get_port() {
        return _uart.get_port();
    }
#endif
};

#endif // DWSERIAL_H
