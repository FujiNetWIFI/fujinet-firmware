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
    void begin(int baud = 0);
    void end();
    bool poll(int ms);

    void set_baudrate(uint32_t baud);
    uint32_t get_baudrate();

    int available();
    void flush();
    void flush_input();

    // read single byte
    int read();
    // read bytes into buffer
    size_t read(uint8_t *buffer, size_t length);
    // alias to read
    size_t readBytes(uint8_t *buffer, size_t length);

    // write single byte
    ssize_t write(uint8_t b);
    // write buffer
    ssize_t write(const uint8_t *buffer, size_t size);
    // write C-string
    ssize_t write(const char *str);

    // print utility functions
    size_t print(const char *str);
    size_t print(std::string str);
    size_t print(int n, int base = 10);
    size_t print(unsigned int n, int base = 10);
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
    dw_mode get_dw_mode() {return _dw_mode;}
    void set_dw_mode(dw_mode mode);

    void reset_dw_port(dw_mode mode);
};

extern DwCom fnDwCom;

#endif // FNDWCOM_H
