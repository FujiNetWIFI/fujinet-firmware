#ifndef FNDWCOM_H
#define FNDWCOM_H

#include <string.h>

#include "dwport.h"
#include "dwbecker.h"
#include "dwserial.h"

/*
 * DriveWire Communication class
 * (replacement for UARTManager fnUartBUS)
 * It uses DwPort for data exchange.
 * DwPort can be physical serial port (SerialDwPort) to communicate with real CoCo computer
 * or Becker port (DriveWire over TCP) for use with CoCO Emulator
 */

class DwCom
{
public:
    // supported DriveWire port types
    enum dw_mode
    {
        SERIAL = 0,
        BECKER
    };

private:
    dw_mode _dw_mode;
    DwPort *_dwPort;
    SerialDwPort _serialDw;
    BeckerPort _beckerDw;

    size_t _print_number(unsigned long n, uint8_t base);

public:
    DwCom();
    void begin(int baud = 0)
    {
        if (baud)
            _dwPort->begin(baud);
        else
            _dwPort->begin(get_baudrate()); // start with default build-in baudrate
    }

    void end() { _dwPort->end(); }

    /*
    * Poll the DriveWire port
    * ms = milliseconds to wait for "port event"
    * return true if port handling is needed
    */
    bool poll(int ms) { return _dwPort->poll(ms); }

    // used only by serial port
    void set_baudrate(uint32_t baud) { _dwPort->set_baudrate(baud); }
    uint32_t get_baudrate() { return _dwPort->get_baudrate(); }

    int available() { return _dwPort->available(); }

    void flush() { _dwPort->flush(); }
    void flush_input() {  _dwPort->flush_input(); }

    // read bytes into buffer
    size_t read(uint8_t *buffer, size_t length) { return _dwPort->read(buffer, length); }
    // alias to read, mimic UARTManager
    size_t readBytes(uint8_t *buffer, size_t length) { return  _dwPort->read(buffer, length); }

    // write buffer
    ssize_t write(const uint8_t *buffer, size_t size) { return _dwPort->write(buffer, size); }
    // write C-string
    ssize_t write(const char *str) { return _dwPort->write((const uint8_t *)str, strlen(str)); }

    // read single byte, mimic UARTManager
    int read();
    // write single byte, mimic UARTManager
    ssize_t write(uint8_t b) { return _dwPort->write(&b, 1); }

    // mimic UARTManager overloaded write functions
    size_t write(unsigned long n) { return write((uint8_t)n); }
    size_t write(long n) { return write((uint8_t)n); }
    size_t write(unsigned int n) { return write((uint8_t)n); }
    size_t write(int n) { return write((uint8_t)n); }

    // print utility functions (used by modem)
    size_t print(const char *str) { return write(str); }
    size_t print(std::string str) { return write(str.c_str()); }
    size_t print(int n, int base = 10) { return print((long) n, base); }
    size_t print(unsigned int n, int base = 10) { return print((unsigned long) n, base); }
    size_t print(long n, int base = 10);
    size_t print(unsigned long n, int base = 10);

    // specific to SerialDwPort
#ifndef ESP_PLATFORM
    void set_serial_port(const char *device);
    const char* get_serial_port();
#endif

    // specific to BeckerPort
    void set_becker_host(const char *host, int port);
    const char* get_becker_host(int &port);

    // get/set DriveWire mode
    dw_mode get_drivewire_mode() {return _dw_mode;}
    void set_drivewire_mode(dw_mode mode);

    void reset_drivewire_port(dw_mode mode);
};

extern DwCom fnDwCom;

#endif // FNDWCOM_H
