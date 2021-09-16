#include "fnSioCom.h"

/*
 * SIO Communication class
 * (replacement for UARTManager fnUartSIO)
 * It uses SioPort for data exchange and to control SIO lines
 * SioPort can be physical serial port (SerialSioPort) to communicate with real Atari computer
 * or network SIO (NetSio = SIO over UDP) for use with Altirra Atari Emulator
 */

SioCom fnSioCom;

SioCom::SioCom() : _netsio_enabled(false), _sioPort(&_serialSio) {}

void SioCom::begin(int baud)
{
    if (baud)
        _sioPort->begin(baud);
    else
        _sioPort->begin(get_baudrate());
}

void SioCom::end() 
{ 
    _sioPort->end(); 
}

void SioCom::set_baudrate(uint32_t baud) 
{ 
    _sioPort->set_baudrate(baud); 
}

uint32_t SioCom::get_baudrate()
{
    return _sioPort->get_baudrate(); 
}

bool SioCom::command_asserted() 
{
    return _sioPort->command_asserted();
}

bool SioCom::motor_asserted() 
{
    return _sioPort->motor_asserted();
}

void SioCom::set_proceed(bool level)
{
    _sioPort->set_proceed(level);
}

void SioCom::set_interrupt(bool level)
{
    _sioPort->set_interrupt(level);
}

int SioCom::available() 
{
    return _sioPort->available();
}

void SioCom::flush() 
{
    _sioPort->flush();
}

void SioCom::flush_input()
{
    _sioPort->flush_input();
}

// read single byte
int SioCom::read()
{
    return _sioPort->read();
}

// read bytes into buffer
size_t SioCom::read(uint8_t *buffer, size_t length)
{
    return _sioPort->read(buffer, length);
}

// alias to read
size_t SioCom::readBytes(uint8_t *buffer, size_t length)
{
    return  _sioPort->read(buffer, length);
}

// write single byte
ssize_t SioCom::write(uint8_t b)
{
    return _sioPort->write(b);
}

// write buffer
ssize_t SioCom::write(const uint8_t *buffer, size_t size) 
{
    return _sioPort->write(buffer, size);
}

// write C-string
ssize_t SioCom::write(const char *str)
{
    return _sioPort->write((const uint8_t *)str, strlen(str));
};

// print utility functions

size_t SioCom::_print_number(unsigned long n, uint8_t base)
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

size_t SioCom::print(const char *str)
{
    return write(str);
}

size_t SioCom::print(std::string str)
{
    return print(str.c_str());
}

size_t SioCom::print(int n, int base)
{
    return print((long) n, base);
}

size_t SioCom::print(unsigned int n, int base)
{
    return print((unsigned long) n, base);
}

size_t SioCom::print(long n, int base)
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

size_t SioCom::print(unsigned long n, int base)
{
    if(base == 0) {
        return write(n);
    } else {
        return _print_number(n, base);
    }
}

// specific to SerialSioPort
void SioCom::setup_serial_port()
{
    _serialSio.setup();
};

// specific to NetSioPort
void SioCom::set_netsio_host(const char *host, int port) 
{
    _netSio.set_host(host, port);
}

const char* SioCom::get_netsio_host(int &port) 
{
    return _netSio.get_host(port);
}

void SioCom::netsio_late_sync(uint8_t c)
{
    _netSio.set_sync_ack_byte(c);
}

void SioCom::netsio_write_size(int write_size)
{
    if (_netsio_enabled)
        _netSio.set_sync_write_size(write_size + 1); // data + checksum byte
}

void SioCom::set_sio_mode(bool enable_netsio)
{
    if (enable_netsio)
        _sioPort = &_netSio;
    else
        _sioPort = &_serialSio;
    _netsio_enabled = enable_netsio;
}

// toggle NetSIOPort and SerialPort
void SioCom::swap_sio_mode(bool enable_netsio)
{
    uint32_t baud = get_baudrate();
    end();
    set_sio_mode(enable_netsio);
    begin(baud);
}
