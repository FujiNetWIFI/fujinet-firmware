#ifndef SIOCOM_H
#define SIOCOM_H

#include <string.h>

#include "sioport.h"
#include "serialsio.h"
// #include "netsio.h"

/*
 * SIO Communication class
 * (replacement for UARTManager fnUartSIO)
 * It uses SioPort for data exchange and to control SIO lines
 * SioPort can be physical serial port (SerialSioPort) to communicate with real Atari computer
 * or network SIO (NetSio = SIO over UDP) for use with Altirra Atari Emulator
 */

class SioCom
{
private:
    bool _netsio_enabled;
    SioPort *_sioPort;
    SerialSioPort _serialSio;
    // NetSioPort _netSio;

    size_t _print_number(unsigned long n, uint8_t base);

public:
    SioCom();
    void begin(int baud = 0);
    void end();

    void set_baudrate(uint32_t baud);
    uint32_t get_baudrate();

    bool command_line();
    bool motor_line();
    void set_proceed_line(bool level);
    void set_interrupt_line(bool level);

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

    // specific to SerialSioPort
    void setup_serial_port();

    // // specific to NetSioPort
    // void set_netsio_host(const char *host, int port);
    // const char* get_netsio_host(int &port);

    // void set_sio_mode(bool enable_netsio);
    // bool get_netsio_enabled() {return _netsio_enabled;}

    // // toggle NetSIOPort and SerialPort
    // void swicth_sio_mode(bool enable_netsio);
};

extern SioCom fnSioCom;

#endif // SIOCOM_H
