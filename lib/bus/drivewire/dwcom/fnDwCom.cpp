#ifdef BUILD_COCO

#include "fnDwCom.h"

#include "../../include/debug.h"
/*
 * DriveWire Communication class
 * (replacement for UARTManager fnUartBUS)
 * It uses DwPort for data exchange.
 * DwPort can be physical serial port (SerialDwPort) to communicate with real CoCo computer
 * or Becker port (DriveWire over TCP) for use with CoCo Emulators
 */

// global instance
DwCom fnDwCom;

// ctor
DwCom::DwCom() : _dw_mode(dw_mode::SERIAL), _dwPort(&_serialDw) {}

// read single byte
int DwCom::read()
{
    uint8_t byte;
    int result = _dwPort->read(&byte, 1);
    if (result < 1)
        return -1;
    return byte;
}

// print utility functions

size_t DwCom::_print_number(unsigned long n, uint8_t base)
{
    char buf[8 * sizeof(long) + 1]; // Assumes 8-bit chars plus zero byte.
    char *str = &buf[sizeof(buf) - 1];

    *str = '\0';

    // prevent crash if called with base == 1
    if(base < 2)
        base = 10;

    do {
        unsigned long m = n;
        n /= base;
        char c = m - base * n;
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while(n);

    return write(str);
}

size_t DwCom::print(long n, int base)
{
    if(base == 0) {
        return write(n);
    } else if(base == 10) {
        if(n < 0) {
            // int t = print('-');
            int t = print("-");
            n = -n;
            return _print_number(n, 10) + t;
        }
        return _print_number(n, 10);
    } else {
        return _print_number(n, base);
    }
}

size_t DwCom::print(unsigned long n, int base)
{
    if(base == 0) {
        return write(n);
    } else {
        return _print_number(n, base);
    }
}

// specific to SerialPort
#ifndef ESP_PLATFORM
void DwCom::set_serial_port(const char *device)
{
    Debug_printf("DwCom::set_serial_port %s\n", device ? device : "NULL");
    _serialDw.set_port(device);
};

const char* DwCom::get_serial_port()
{
    return _serialDw.get_port();
};
#endif

// specific to BeckerPort
void DwCom::set_becker_host(const char *host, int port) 
{
    Debug_printf("DwCom::set_becker_host %s:%d\n", host ? host : "NULL", port);
    _beckerDw.set_host(host, port);
}

const char* DwCom::get_becker_host(int &port) 
{
    return _beckerDw.get_host(port);
}

void DwCom::set_drivewire_mode(dw_mode mode)
{
    Debug_printf("DwCom::set_drivewire_mode: %s\n", mode == dw_mode::BECKER ? "BECKER" : "SERIAL");
    _dw_mode = mode;
    switch(mode)
    {
    case dw_mode::BECKER:
        _dwPort = &_beckerDw;
        break;
    default:
        _dwPort = &_serialDw;
    }
}

// toggle BeckerPort and SerialPort
void DwCom::reset_drivewire_port(dw_mode mode)
{
    uint32_t baud = get_baudrate();
    end();
    set_drivewire_mode(mode);
    begin(baud);
}

#endif // BUILD_COCO
