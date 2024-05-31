#ifndef SERIALSIO_H
#define SERIALSIO_H

#include <stdio.h>

#include "fnUART.h"
#include "sioport.h"

/*
 * Implementation of SIO Port using UART serial port
 * wrapper around existing UARTManager
 */

class SerialSioPort : public SioPort
{
private:
    UARTManager _uart;
public:
    SerialSioPort() {}
    virtual void begin(int baud) override { _uart.begin(baud); }
    virtual void end() override { _uart.end(); }
    virtual bool poll(int ms) override { return _uart.poll(ms); }

    virtual void set_baudrate(uint32_t baud) override { _uart.set_baudrate(baud); }
    virtual uint32_t get_baudrate() override { return _uart.get_baudrate(); }

    virtual bool command_asserted() override { return _uart.command_asserted(); }
    virtual bool motor_asserted() override { return _uart.motor_asserted(); }
    virtual void set_proceed(bool level) override { _uart.set_proceed(level); }
    virtual void set_interrupt(bool level) override { _uart.set_interrupt(level); }

    virtual void bus_idle(uint16_t ms) override;

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

    // specific to SerialSioPort/UART
    void set_port(const char *device, int command_pin, int proceed_pin) {
        _uart.set_port(device, command_pin, proceed_pin);
    }
    const char* get_port(int &command_pin, int &proceed_pin) {
        return _uart.get_port(&command_pin, &proceed_pin);
    }
};

#endif // SERIALSIO_H
