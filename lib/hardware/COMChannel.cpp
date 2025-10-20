#include "COMChannel.h"

#ifdef HELLO_IM_A_PC

#include "../../include/debug.h"

void COMChannel::begin(const ChannelConfig& conf)
{
    if (_fd != INVALID_HANDLE_VALUE)
        end();

    _device = conf.device;
    read_timeout_ms = conf.read_timeout_ms;
    discard_timeout_ms = conf.discard_timeout_ms;

    // Open the serial port
    if (_device.empty())
    {
        Debug_println("Serial port is not configured!");
        return;
    }

    Debug_printf("Setting up serial port %s\n", _device.c_str());
    _fd = CreateFile(_device.c_str(),
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
        return;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength=sizeof(dcbSerialParams);

    if (!GetCommState(_fd, &dcbSerialParams))
    {
        //error getting state
        Debug_printf("GetCommState error: %d\n", GetLastError());
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
        return;
    }

    Debug_printf("### UART initialized ###\n");
    setBaudrate(conf.baud_rate);

    return;
}

void COMChannel::end()
{
    if (_fd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_fd);
        _fd = INVALID_HANDLE_VALUE;
        Debug_printf("### UART stopped ###\n");
    }
    return;
}

void COMChannel::updateFIFO()
{
    COMSTAT cs;

    if (_fd == INVALID_HANDLE_VALUE)
        return;

    if (!ClearCommError(_fd, NULL, &cs))
        return;
    if (!cs.cbInQue)
        return;

    size_t old_len = _fifo.size();
    DWORD rxbytes;
    _fifo.resize(old_len + cs.cbInQue);
    if (!ReadFile(_fd, &_fifo[old_len], cs.cbInQue, &rxbytes, NULL))
        rxbytes = 0;
    _fifo.resize(old_len + rxbytes);

    return;
}

size_t COMChannel::dataOut(const void *buffer, size_t length)
{
    DWORD txbytes;
    if (!WriteFile(_fd, buffer, (DWORD) length, &txbytes, NULL))
    {
        Debug_printf("UART write() write error: %dn", GetLastError());
    }
    return (size_t)txbytes;
}

void COMChannel::flushOutput()
{
    if (_fd != INVALID_HANDLE_VALUE)
        FlushFileBuffers(_fd);
    return;
}

void COMChannel::setBaudrate(uint32_t newBaud)
{
    Debug_printf("UART set_baudrate: %d\n", newBaud);

    if (_fd == INVALID_HANDLE_VALUE || newBaud == _baud)
        return;

    _baud = newBaud;

    if (_baud == 0)
        _baud = 19200;

    int baud_id = 0;

    // map baud rate to predefined constant
    switch (_baud)
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
        baud_id = _baud;
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

    return;
}

bool COMChannel::getDTR()
{
    DWORD status;

    if (!GetCommModemStatus(_fd, &status))
        return false;
    return !!(status & MS_DSR_ON);
}

void COMChannel::setDSR(bool state)
{
    int status;

    if (state == _dtrState)
        return;

    _dtrState = state;
    EscapeCommFunction(_fd, _dtrState ? SETDTR : CLRDTR);
    return;
}

bool COMChannel::getRTS()
{
    DWORD status;

    if (!GetCommModemStatus(_fd, &status))
        return false;
    return !!(status & MS_CTS_ON);
}

void COMChannel::setCTS(bool state)
{
    int status;

    if (state == _dtrState)
        return;

    _dtrState = state;
    EscapeCommFunction(_fd, _dtrState ? SETRTS : CLRRTS);
    return;
}

bool COMChannel::getRI()
{
    DWORD status;

    if (!GetCommModemStatus(_fd, &status))
        return false;
    return !!(status & MS_RING_ON);
}

#endif /* HELLO_IM_A_PC */
