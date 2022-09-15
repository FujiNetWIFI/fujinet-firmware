#ifdef BUILD_ATARI

#include "fnSioLink.h"

/*
 * SIO Link class
 * (replacement for UARTManager fnUartSIO)
 * It uses SioPort for data exchange and to control SIO lines
 * SioPort can be physical serial port (SerialSioPort) to communicate with real Atari computer
 * or network SIO (NetSioPort = SIO over UDP) for use with Altirra Atari Emulator
 */

SioLink fnSioLink;

SioLink::SioLink() : _sio_mode(sio_mode::SERIAL), _sioPort(&_serialSio) {}

void SioLink::begin(int baud)
{
    if (baud)
        _sioPort->begin(baud);
    else
        _sioPort->begin(get_baudrate());
}

void SioLink::end() 
{ 
    _sioPort->end(); 
}

void SioLink::set_baudrate(uint32_t baud) 
{ 
    _sioPort->set_baudrate(baud); 
}

uint32_t SioLink::get_baudrate()
{
    return _sioPort->get_baudrate(); 
}

bool SioLink::command_asserted() 
{
    return _sioPort->command_asserted();
}

bool SioLink::motor_asserted() 
{
    return _sioPort->motor_asserted();
}

void SioLink::set_proceed(bool level)
{
    _sioPort->set_proceed(level);
}

void SioLink::set_interrupt(bool level)
{
    _sioPort->set_interrupt(level);
}

int SioLink::available() 
{
    return _sioPort->available();
}

void SioLink::flush() 
{
    _sioPort->flush();
}

void SioLink::flush_input()
{
    _sioPort->flush_input();
}

// read single byte
int SioLink::read()
{
    return _sioPort->read();
}

// read bytes into buffer
size_t SioLink::read(uint8_t *buffer, size_t length)
{
    return _sioPort->read(buffer, length);
}

// alias to read
size_t SioLink::readBytes(uint8_t *buffer, size_t length)
{
    return  _sioPort->read(buffer, length);
}

// write single byte
ssize_t SioLink::write(uint8_t b)
{
    return _sioPort->write(b);
}

// write buffer
ssize_t SioLink::write(const uint8_t *buffer, size_t size) 
{
    return _sioPort->write(buffer, size);
}

// write C-string
ssize_t SioLink::write(const char *str)
{
    return _sioPort->write((const uint8_t *)str, strlen(str));
};

// print utility functions

size_t SioLink::_print_number(unsigned long n, uint8_t base)
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

size_t SioLink::print(const char *str)
{
    return write(str);
}

size_t SioLink::print(std::string str)
{
    return print(str.c_str());
}

size_t SioLink::print(int n, int base)
{
    return print((long) n, base);
}

size_t SioLink::print(unsigned int n, int base)
{
    return print((unsigned long) n, base);
}

size_t SioLink::print(long n, int base)
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

size_t SioLink::print(unsigned long n, int base)
{
    if(base == 0) {
        return write(n);
    } else {
        return _print_number(n, base);
    }
}

// specific to SerialSioPort
void SioLink::setup_serial_port()
{
    _serialSio.setup();
};

// specific to NetSioPort
void SioLink::set_netsio_host(const char *host, int port) 
{
    _netSio.set_host(host, port);
}

const char* SioLink::get_netsio_host(int &port) 
{
    return _netSio.get_host(port);
}

void SioLink::netsio_late_sync(uint8_t c)
{
    _netSio.set_sync_ack_byte(c);
}

void SioLink::netsio_empty_sync()
{
    _netSio.send_empty_sync();
}

void SioLink::netsio_write_size(int write_size)
{
    _netSio.set_sync_write_size(write_size + 1); // data + checksum byte
}

void SioLink::set_sio_mode(sio_mode mode)
{
    _sio_mode = mode;
    switch(mode)
    {
    case sio_mode::NETSIO:
        _sioPort = &_netSio;
        break;
    default:
        _sioPort = &_serialSio;
    }
}

// toggle NetSIOPort and SerialPort
void SioLink::reset_sio_port(sio_mode mode)
{
    uint32_t baud = get_baudrate();
    end();
    set_sio_mode(mode);
    begin(baud);
}

#endif /* BUILD_ATARI */