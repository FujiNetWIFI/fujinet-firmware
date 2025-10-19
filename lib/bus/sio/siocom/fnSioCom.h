#ifndef SIOCOM_H
#define SIOCOM_H

#include <string.h>

#include "sioport.h"
#include "netsio.h"
#include "serialsio.h"

/*
 * SIO Communication class
 * (replacement for UARTManager fnUartSIO)
 * It uses SioPort for data exchange and to control SIO lines
 * SioPort can be physical serial port (SerialSioPort) to communicate with real Atari computer
 * or network SIO (NetSio = SIO over UDP) for use with Altirra Atari Emulator
 */

class SioCom
{
public:
    // supported SIO port types
    enum sio_mode
    {
        SERIAL = 0,
        NETSIO
    };

private:
    sio_mode _sio_mode;
    SioPort *_sioPort;
    SerialSioPort _serialSio;
    NetSioPort _netSio;

    size_t _print_number(unsigned long n, uint8_t base);

public:
    SioCom();
    void begin(int baud = 0);
    void end();
    bool poll(int ms);

    void set_baudrate(uint32_t baud);
    uint32_t get_baudrate();

    bool command_asserted();
    bool motor_asserted();
    void set_proceed(bool level);
    void set_interrupt(bool level);

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

    void bus_idle(uint16_t ms);

    // specific to SerialSioPort
    void set_serial_port(const char *device, int command_pin, int proceed_pin);
    const char* get_serial_port(int &command_pin, int &proceed_pin);

    // specific to NetSioPort
    void set_netsio_host(const char *host, int port);
    const char* get_netsio_host(int &port);
    void netsio_late_sync(uint8_t c);
    void netsio_empty_sync();
    void netsio_write_size(int write_size);

    // get/set SIO mode
    sio_mode get_sio_mode() {return _sio_mode;}
    void set_sio_mode(sio_mode mode);

    void reset_sio_port(sio_mode mode);
};

#endif // SIOCOM_H
