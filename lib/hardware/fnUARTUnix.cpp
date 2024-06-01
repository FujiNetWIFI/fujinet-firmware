#ifndef ESP_PLATFORM

#if defined(__linux__) || defined(__APPLE__)

// Linux and macOS UART code

#include "fnUART.h"

#include <string.h>
#include <cstdarg>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h> // write(), read(), close()
#include <errno.h> // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <sys/ioctl.h> // TIOCM_DSR etc.
#include <fcntl.h> // Contains file controls like O_RDWR

#if defined(__linux__)
#include <linux/serial.h>
#include "linux_termios2.h"
#elif defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#endif

#include "compat_string.h"
#include "fnSystem.h"

#include "../../include/debug.h"

#define UART_PROBE_DEV1 "/dev/ttyUSB0"
#define UART_PROBE_DEV2 "/dev/ttyS0"
#define UART_DEFAULT_BAUD 19200


// Constructor
// UARTManager::UARTManager(uart_port_t uart_num) : _uart_num(uart_num), _uart_q(NULL) {}
UARTManager::UARTManager() :
    _initialized(false),
    _fd(-1),
    _device{0},
    _baud(UART_DEFAULT_BAUD)
{};

void UARTManager::end()
{
    if (_fd >= 0)
    {
        close(_fd);
        _fd  = -1;
        Debug_printf("### UART stopped ###\n");
    }
    _initialized = false;
}

bool UARTManager::poll(int ms)
{
    // TODO check serial port command link and input data
    fnSystem.delay_microseconds(500); // TODO use ms parameter
    return false;
}

void UARTManager::set_port(const char *device, int command_pin, int proceed_pin)
{
    if (device != nullptr)
        strlcpy(_device, device, sizeof(_device));

    _command_pin = command_pin;
    _proceed_pin = proceed_pin;

    switch (_command_pin)
    {
// TODO move from fnConfig.h to fnUART.h
// enum serial_command_pin
// {
//     SERIAL_COMMAND_NONE = 0,
//     SERIAL_COMMAND_DSR,
//     SERIAL_COMMAND_CTS,
//     SERIAL_COMMAND_RI,
//     SERIAL_COMMAND_INVALID
// };
    case 1: // SERIAL_COMMAND_DSR
        _command_tiocm = TIOCM_DSR;
        break;
    case 2: // SERIAL_COMMAND_CTS
        _command_tiocm = TIOCM_CTS;
        break;
    case 3: // SERIAL_COMMAND_RI
        _command_tiocm = TIOCM_RI;
        break;
    default:
        _command_tiocm = 0;
    }

// TODO move from fnConfig.h to fnUART.h
// enum serial_proceed_pin
// {
//     SERIAL_PROCEED_NONE = 0,
//     SERIAL_PROCEED_DTR,
//     SERIAL_PROCEED_RTS,
//     SERIAL_PROCEED_INVALID
// };
    switch (_proceed_pin)
    {
    case 1: // SERIAL_PROCEED_DTR
        _proceed_tiocm = TIOCM_DTR;
        break;
    case 2: // SERIAL_PROCEED_RTS
        _proceed_tiocm = TIOCM_RTS;
        break;
    default:
        _proceed_tiocm = 0;
    }
}

const char* UARTManager::get_port(int *ptr_command_pin, int *ptr_proceed_pin)
{
    if (ptr_command_pin)
        *ptr_command_pin = _command_pin;
    if (ptr_proceed_pin)
        *ptr_proceed_pin = _proceed_pin;
    return _device;
}

void UARTManager::begin(int baud)
{
    if(_initialized)
    {
        end();
    }

    _errcount = 0;
    _suspend_time = 0;

    // Open the serial port
    if (*_device == 0)
    {
        // Probe some serial ports
        Debug_println("Trying " UART_PROBE_DEV1);
        if ((_fd = open(UART_PROBE_DEV1, O_RDWR | O_NOCTTY | O_NONBLOCK)) >= 0)
            strlcpy(_device, UART_PROBE_DEV1, sizeof(_device));
        else
        {
            Debug_println("Trying " UART_PROBE_DEV2);
            if ((_fd = open(UART_PROBE_DEV2, O_RDWR | O_NOCTTY | O_NONBLOCK)) >= 0)
                strlcpy(_device, UART_PROBE_DEV2, sizeof(_device));
        }

        // successful probe
        if (*_device != 0)
            Debug_printf("Setting up serial port %s\n", _device);
    }
    else
    {
        Debug_printf("Setting up serial port %s\n", _device);
        _fd = open(_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    }

    if (_fd < 0)
    {
        Debug_printf("Failed to open serial port, error %d: %s\n", errno, strerror(errno));
        suspend();
		return;
	}

#if defined(__linux__)
    // Enable low latency
	struct serial_struct ss;
    if (ioctl(_fd, TIOCGSERIAL, &ss) == -1)
    {
        Debug_printf("UART warning: TIOCGSERIAL failed: %d - %s\n", errno, strerror(errno));
    }
    else
    {
        ss.flags |= ASYNC_LOW_LATENCY;
        if (ioctl(_fd, TIOCSSERIAL, &ss) == -1)
            Debug_printf("UART warning: TIOCSSERIAL failed: %d - %s\n", errno, strerror(errno));
    }
#endif

    // Read in existing settings
    struct termios tios;
    if(tcgetattr(_fd, &tios) != 0)
    {
        Debug_printf("tcgetattr error %d: %s\n", errno, strerror(errno));
        suspend();
		return;
    }

    tios.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    tios.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    tios.c_cflag &= ~CSIZE; // Clear all bits that set the data size 
    tios.c_cflag |= CS8; // 8 bits per byte (most common)
    tios.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
    tios.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)
  
    tios.c_lflag &= ~ICANON;
    tios.c_lflag &= ~ECHO; // Disable echo
    tios.c_lflag &= ~ECHOE; // Disable erasure
    tios.c_lflag &= ~ECHONL; // Disable new-line echo
    tios.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tios.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tios.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
  
    tios.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tios.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
    // tios.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
    // tios.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

    tios.c_cc[VTIME] = 0;
    tios.c_cc[VMIN] = 0;

    // Apply settings
    if (tcsetattr(_fd, TCSANOW, &tios) != 0)
    {
        Debug_printf("tcsetattr error %d: %s\n", errno, strerror(errno));
        suspend();
		return;
    }

    Debug_printf("### UART initialized ###\n");
    // Set initialized.
    _initialized = true;
    set_baudrate(baud);
}


void UARTManager::suspend(int sec)
{
    Debug_println("Suspending serial port");
    struct timeval tv;
    gettimeofday(&tv, NULL);
    _suspend_time = tv.tv_sec + sec;
    end();
}

/* Discards anything in the input buffer
 */
void UARTManager::flush_input()
{
    if (_initialized)
        tcflush(_fd, TCIFLUSH);
}

/* Clears input buffer and flushes out transmit buffer waiting at most
   waiting MAX_FLUSH_WAIT_TICKS until all sends are completed
*/
void UARTManager::flush()
{
    if (_initialized)
        tcdrain(_fd);
}

/* Returns number of bytes available in receive buffer or -1 on error
 */
int UARTManager::available()
{
    int result;
    if (!_initialized)
        return 0;
	if (ioctl(_fd, FIONREAD, &result) < 0)
        return 0;
    return result;
}

/* Changes baud rate
*/
void UARTManager::set_baudrate(uint32_t baud)
{
    Debug_printf("UART set_baudrate: %d\n", baud);

    if (!_initialized)
        return;

    if (baud == 0)
        baud = 19200;

    int baud_id = B0;  // B0 to indicate custom speed

    // map baudrate to predefined constant
    switch (baud)
    {
    case 300:
        baud_id = B300;
        break;
    case 600:
        baud_id = B600;
        break;
    case 1200:
        baud_id = B1200;
        break;
    case 1800:
        baud_id = B1800;
        break;
    case 2400:
        baud_id = B2400;
        break;
    case 4800:
        baud_id = B4800;
        break;
    case 9600:
        baud_id = B9600;
        break;
    case 19200:
        baud_id = B19200;
        break;
    case 38400:
        baud_id = B38400;
        break;
    case 57600:
        baud_id = B57600;
        break;
    case 115200:
        baud_id = B115200;
        break;
    }

    if (baud_id == B0)
    {
        // custom baud rate
#if defined(__APPLE__) && defined (IOSSIOSPEED)
        // OS X support

        speed_t new_baud = (speed_t) baud;
        if (-1 == ioctl(_fd, IOSSIOSPEED, &new_baud, 1))
            Debug_printf("IOSSIOSPEED error %d: %s\n", errno, strerror(errno));

#elif defined(__linux__)
        // Linux Support

		struct termios2 tios2;
		
		if (-1 == ioctl(_fd, TCGETS2, &tios2))
        {
            Debug_printf("TCGETS2 error %d: %s\n", errno, strerror(errno));
            return;
		}
		tios2.c_cflag &= ~(CBAUD | CBAUD << LINUX_IBSHIFT);
		tios2.c_cflag |= BOTHER | BOTHER << LINUX_IBSHIFT;
		tios2.c_ispeed = baud;
		tios2.c_ospeed = baud;
		if (-1 == ioctl(_fd, TCSETS2, &tios2))
            Debug_printf("TCSETS2 error %d: %s\n", errno, strerror(errno));
#else
        Debug_println("Custom baud rate is not implemented");
#endif
    }
    else
    {
        // standard speeds
        termios tios;
        if(tcgetattr(_fd, &tios) != 0)
            Debug_printf("tcgetattr error %d: %s\n", errno, strerror(errno));
        cfsetspeed(&tios, baud_id);
        // Apply settings
        if (tcsetattr(_fd, TCSANOW, &tios) != 0)
            Debug_printf("tcsetattr error %d: %s\n", errno, strerror(errno));
    }
    _baud = baud;
}

bool UARTManager::command_asserted(void)
{
    int status;

    if (! _initialized)
    {
        // is serial port suspended ?
        if (_suspend_time != 0)
        {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            if (_suspend_time > tv.tv_sec)
                return false;
            // try to re-open serial port
            begin(_baud);
        }
        if (! _initialized)
            return false;
    }

    if (ioctl(_fd, TIOCMGET, &status) < 0)
    {
        // handle serial port errors
        _errcount++;
        if(_errcount == 1)
            Debug_printf("UART command_asserted() TIOCMGET error %d: %s\n", errno, strerror(errno));
        else if (_errcount > 1000)
            suspend();
        return false;
    }
    _errcount = 0;

    return ((status & _command_tiocm) != 0);
}

void UARTManager::set_proceed(bool level)
{
    static int last_level = -1; // 0,1 or -1 for unknown
    int new_level = level ? 0 : 1;
    int result;

    if (!_initialized)
        return;
    if (last_level == new_level)
        return;

    Debug_print(level ? "+" : "-");
    last_level = new_level;

    unsigned long request = level ? TIOCMBIC : TIOCMBIS;
    result = ioctl(_fd, request, &_proceed_tiocm);
    if (result < 0)
        Debug_printf("UART set_proceed() ioctl error %d: %s\n", errno, strerror(errno));
}

timeval timeval_from_ms(const uint32_t millis)
{
  timeval tv;
  tv.tv_sec = millis / 1000;
  tv.tv_usec = (millis - (tv.tv_sec * 1000)) * 1000;
  return tv;
}

bool UARTManager::waitReadable(uint32_t timeout_ms)
{
    // Setup a select call to block for serial data or a timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(_fd, &readfds);
    timeval timeout_tv(timeval_from_ms(timeout_ms));

    int result = select(_fd + 1, &readfds, nullptr, nullptr, &timeout_tv);

    if (result < 0) 
    {
        if (errno != EINTR)
        {
            Debug_printf("UART waitReadable() select error %d: %s\n", errno, strerror(errno));
        }
        return false;
    }
    // Timeout occurred
    if (result == 0)
    {
        return false;
    }
    // This shouldn't happen, if result > 0 our fd has to be in the list!
    if (!FD_ISSET (_fd, &readfds)) 
    {
        Debug_println("UART waitReadable() unexpected select result");
    }
    // Data available to read.
    return true;
}

/* Returns a single byte from the incoming stream
 */
int UARTManager::read(void)
{
    uint8_t byte;
    return (readBytes(&byte, 1) == 1) ? byte : -1;
}

/* Since the underlying Stream calls this Read() multiple times to get more than one
 *  character for ReadBytes(), we override with a single call to uart_read_bytes
 */
size_t UARTManager::readBytes(uint8_t *buffer, size_t length)
{
    if (!_initialized)
        return 0;

    int result;
    int rxbytes;
    for (rxbytes=0; rxbytes<length;)
    {
        result = ::read(_fd, &buffer[rxbytes], length-rxbytes);
        // Debug_printf("read: %d\n", result);
        if (result < 0)
        {
            if (errno == EAGAIN)
            {
                result = 0;
            }
            else
            {
                Debug_printf("UART readBytes() read error %d: %s\n", errno, strerror(errno));
                break;
            }
        }

        rxbytes += result;
        if (rxbytes == length)
        {
            // done
            break;
        }

        if (!waitReadable(500)) // 500 ms timeout
        {
            Debug_println("UART readBytes() TIMEOUT");
            break;
        }
    }
    return rxbytes;
}

size_t UARTManager::write(uint8_t c)
{
    // int z = uart_write_bytes(_uart_num, (const char *)&c, 1);
    // //uart_wait_tx_done(_uart_num, MAX_WRITE_BYTE_TICKS);
    // return z;
    return write(&c, 1);
}

size_t UARTManager::write(const uint8_t *buffer, size_t size)
{
    if (!_initialized)
        return 0;

    int result;
    int txbytes;
    fd_set writefds;
    timeval timeout_tv(timeval_from_ms(500));

    for (txbytes=0; txbytes<size;)
    {
        FD_ZERO(&writefds);
        FD_SET(_fd, &writefds);

        // Debug_printf("select(%lu)\n", timeout_tv.tv_sec*1000+timeout_tv.tv_usec/1000);
        int result = select(_fd + 1, NULL, &writefds, NULL, &timeout_tv);

        if (result < 0) 
        {
            // Select was interrupted, try again
            if (errno == EINTR) 
            {
                continue;
            }
            // Otherwise there was some error
            Debug_printf("UART write() select error %d: %s\n", errno, strerror(errno));
            break;
        }

        // Timeout
        if (result == 0)
        {
            Debug_println("UART write() TIMEOUT");
            break;
        }

        if (result > 0) 
        {
            // Make sure our file descriptor is in the ready to write list
            if (FD_ISSET(_fd, &writefds))
            {
                // This will write some
                result = ::write(_fd, &buffer[txbytes], size-txbytes);
                // Debug_printf("write: %d\n", result);
                if (result < 1)
                {
                    Debug_printf("UART write() write error %d: %s\n", errno, strerror(errno));
                    break;
                }

                txbytes += result;
                if (txbytes == size)
                {
                    // TODO is flush missing somewhere else?
                    // wait UART is writable again before return
                    timeout_tv = timeval_from_ms(1000 + result * 12500 / _baud);
                    select(_fd + 1, NULL, &writefds, NULL, &timeout_tv);
                    // done
                    break;
                }
                if (txbytes < size)
                {
                    timeout_tv = timeval_from_ms(1000 + result * 12500 / _baud);
                    continue;
                }
            }
            // This shouldn't happen, if r > 0 our fd has to be in the list!
            Debug_println("UART write() unexpected select result");
        }
    }
    return txbytes;
}

size_t UARTManager::write(const char *str)
{
    return write((const uint8_t *)str, strlen(str));
}

#endif // __linux__ || __APPLE__

#endif // !ESP_PLATFORM