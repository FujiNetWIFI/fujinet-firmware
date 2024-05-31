#ifndef ESP_PLATFORM

#if defined(_WIN32)

// Windows UART code

#include "fnUART.h"

#include <string.h>
#include <cstdarg>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h> // write(), read(), close()
#include <errno.h> // Error integer and strerror() function
#include <fcntl.h> // Contains file controls like O_RDWR

#include "compat_string.h"
#include "fnSystem.h"

#include "../../include/debug.h"

#define UART_DEFAULT_BAUD 19200


// Constructor
// UARTManager::UARTManager(uart_port_t uart_num) : _uart_num(uart_num), _uart_q(NULL) {}
UARTManager::UARTManager() :
    _initialized(false),
    _fd(INVALID_HANDLE_VALUE),
    _device{0},
    _baud(UART_DEFAULT_BAUD)
{};

void UARTManager::end()
{
    if (_fd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_fd);
        _fd  = INVALID_HANDLE_VALUE;
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
        _command_status = MS_DSR_ON;
        break;
    case 2: // SERIAL_COMMAND_CTS
        _command_status = MS_CTS_ON;
        break;
    case 3: // SERIAL_COMMAND_RI
        _command_status = MS_RING_ON;
        break;
    default:
        _command_status = 0;
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
        _proceed_set = SETDTR;
        _proceed_clear = CLRDTR;
        break;
    case 2: // SERIAL_PROCEED_RTS
        _proceed_set = SETRTS;
        _proceed_clear = CLRRTS;
        break;
    default:
        _proceed_set = 0;
        _proceed_clear = 0;
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
        Debug_println("Serial port is not configured!");
        suspend();
		return;
    }
    //wstring port_with_prefix = _prefix_port_if_needed(port_);
    //LPCWSTR lp_port = port_with_prefix.c_str();
    Debug_printf("Setting up serial port %s\n", _device);
    _fd = CreateFile(_device,
                     GENERIC_READ | GENERIC_WRITE,
                     0,
                     0,
                     OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL,
                     0);

    if (_fd == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        Debug_printf("Failed to open serial port, error: %d\n", err);
        if (err == ERROR_FILE_NOT_FOUND)
        {
            Debug_printf("Device not found\n");
        }
        suspend();
		return;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength=sizeof(dcbSerialParams);

    if (!GetCommState(_fd, &dcbSerialParams)) 
    {
        //error getting state
        Debug_printf("GetCommState error: %d\n", GetLastError());
        suspend();
        return;
    }

    dcbSerialParams.ByteSize = 8; // 8 bits per byte
    dcbSerialParams.StopBits = ONESTOPBIT; // one stop bit
    dcbSerialParams.Parity = NOPARITY; // no parity
    // no flowcontrol
    dcbSerialParams.fOutxCtsFlow = false;
    dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE;
    dcbSerialParams.fOutX = false;
    dcbSerialParams.fInX = false;

    // activate settings
    if (!SetCommState(_fd, &dcbSerialParams)){
        Debug_printf("SetCommState error: %d\n", GetLastError());
        suspend();
        return;
    }

    Debug_printf("### UART initialized ###\n");
    // Set initialized.
    _initialized=true;
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

void UARTManager::flush_input()
{
    if (_initialized)
        PurgeComm(_fd, PURGE_RXCLEAR);
}

/* Clears input buffer and flushes out transmit buffer waiting at most
   waiting MAX_FLUSH_WAIT_TICKS until all sends are completed
*/
void UARTManager::flush()
{
    if (_initialized)
        FlushFileBuffers(_fd);
}

/* Returns number of bytes available in receive buffer or -1 on error
*/
int UARTManager::available()
{
    if (!_initialized)
        return 0;

    COMSTAT cs;
    if (!ClearCommError(_fd, NULL, &cs))
        return 0;
    return (size_t)cs.cbInQue;
}

void UARTManager::set_baudrate(uint32_t baud)
{
    Debug_printf("UART set_baudrate: %d\n", baud);

    if (!_initialized)
        return;

    if (baud == 0)
        baud = 19200;

    int baud_id = 0;

    // map baud rate to predefined constant
    switch (baud)
    {
#ifdef CBR_300
    case 300:
        baud_id = CBR_300;
        break;
#endif
#ifdef CBR_600
    case 600:
        baud_id = CBR_600;
        break;
#endif
#ifdef CBR_1200
    case 1200:
        baud_id = CBR_1200;
        break;
#endif
#ifdef CBR_1800
    case 1800:
        baud_id = CBR_1800;
        break;
#endif
#ifdef CBR_2400
    case 2400:
        baud_id = CBR_2400;
        break;
#endif
#ifdef CBR_4800
    case 4800:
        baud_id = CBR_4800;
        break;
#endif
#ifdef CBR_9600
    case 9600:
        baud_id = CBR_9600;
        break;
#endif
#ifdef CBR_19200
    case 19200:
        baud_id = CBR_19200;
        break;
#endif
#ifdef CBR_38400
    case 38400:
        baud_id = CBR_38400;
        break;
#endif
#ifdef CBR_57600
    case 57600:
        baud_id = CBR_57600;
        break;
#endif
#ifdef CBR_115200
    case 115200:
        baud_id = CBR_115200;
        break;
#endif
    default:
        // no constant for given baud rate, try to blindly assign it
        baud_id = baud;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength=sizeof(dcbSerialParams);

    if (!GetCommState(_fd, &dcbSerialParams)) 
    {
        //error getting state
        Debug_printf("GetCommState error: %d\n", GetLastError());
        return;
    }

    dcbSerialParams.BaudRate = baud_id;

    // activate settings
    if (!SetCommState(_fd, &dcbSerialParams)){
        Debug_printf("SetCommState error: %d\n", GetLastError());
        return;
    }

    // Setup timeouts
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.ReadTotalTimeoutMultiplier = 50;
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 50;
    if (!SetCommTimeouts(_fd, &timeouts)) 
    {
        Debug_printf("Error setting timeouts.\n");
    }

    _baud = baud;
}

bool UARTManager::command_asserted(void)
{
    DWORD status;

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

    if (!GetCommModemStatus(_fd, &status))
    {
        // handle serial port errors
        _errcount++;
        if(_errcount == 1)
            Debug_printf("UART command_asserted() GetCommModemStatus error: %d\n", GetLastError());
        else if (_errcount > 1000)
            suspend();
        return false;
    }
    _errcount = 0;

    return ((status & _command_status) != 0);
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

    if (level) {
        EscapeCommFunction(_fd, _proceed_set);
    } else {
        EscapeCommFunction(_fd, _proceed_clear);
    }
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
    return false;
}

int UARTManager::read(void)
{
    uint8_t byte;
    return (readBytes(&byte, 1) == 1) ? byte : -1;
}

size_t UARTManager::readBytes(uint8_t *buffer, size_t length)
{
    if (!_initialized)
        return 0;

    DWORD rxbytes;
    if (!ReadFile(_fd, buffer, (DWORD)length, &rxbytes, NULL))
    {
        Debug_printf("UART readBytes() read error: %d\n", GetLastError());
    }
    return (size_t)(rxbytes);
}

size_t UARTManager::write(uint8_t c)
{
    return write(&c, 1);
}

size_t UARTManager::write(const uint8_t *buffer, size_t size)
{
    DWORD txbytes;
    if (!WriteFile(_fd, buffer, (DWORD)size, &txbytes, NULL)) 
    {
        Debug_printf("UART write() write error: %dn", GetLastError());
    }
    return (size_t)txbytes;
}

size_t UARTManager::write(const char *str)
{
    return write((const uint8_t *)str, strlen(str));
}

#endif // _WIN32

#endif // !ESP_PLATFORM