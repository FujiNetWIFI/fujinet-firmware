#ifndef SIOPORT_H
#define SIOPORT_H

#include <stdint.h>
#include <sys/types.h>

# define SIOPORT_DEFAULT_BAUD   19200

/*
 * Abstraction of SIO port
 * provides interface to basic functionality and signals
 */

class SioPort
{
public:
    virtual void begin(int baud) = 0;
    virtual void end() = 0;

    virtual void set_baudrate(uint32_t baud) = 0;
    virtual uint32_t get_baudrate() = 0;

    virtual bool command_asserted() = 0;
    virtual bool motor_asserted() = 0;
    virtual void set_proceed(bool level) = 0;
    virtual void set_interrupt(bool level) = 0;

    virtual int available() = 0;
    virtual void flush() = 0;
    virtual void flush_input() = 0;

    virtual int read() = 0; // read single byte
    virtual size_t read(uint8_t *buffer, size_t length) = 0; // read bytes into buffer

    virtual ssize_t write(uint8_t b) = 0; // write single byte
    virtual ssize_t write(const uint8_t *buffer, size_t size) = 0; // write buffer
};

#endif // SIOPORT_H
