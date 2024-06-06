#ifndef DWPORT_H
#define DWPORT_H

#include <stdint.h>
#include <sys/types.h>

/*
 * Abstraction of DriveWire port
 * provides interface to basic functionality and signals
 */

class DwPort
{
public:
    virtual void begin(int baud) = 0;
    virtual void end() = 0;
    virtual bool poll(int ms) = 0;

    virtual void set_baudrate(uint32_t baud) = 0;
    virtual uint32_t get_baudrate() = 0;

    virtual int available() = 0;
    virtual void flush() = 0;
    virtual void flush_input() = 0;

    virtual size_t read(uint8_t *buffer, size_t size) = 0; // read bytes into buffer
    virtual ssize_t write(const uint8_t *buffer, size_t size) = 0; // write buffer
};

#endif // DWPORT_H
