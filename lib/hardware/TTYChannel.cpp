#include "TTYChannel.h"

#ifdef ITS_A_UNIX_SYSTEM_I_KNOW_THIS

#include <fcntl.h> // Contains file controls like O_RDWR
#include <unistd.h>

#if defined(__linux__)
#include <linux/serial.h>
#include "linux_termios2.h"
#elif defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#include <sys/ioctl.h>
#endif

#include "../../include/debug.h"

#define TTY_PROBE_DEV1 "/dev/ttyUSB0"
#define TTY_PROBE_DEV2 "/dev/ttyS0"

void TTYChannel::begin(const ChannelConfig& conf)
{
    if (_fd >= 0)
        end();

    _device = conf.device;
    read_timeout_ms = conf.read_timeout_ms;
    discard_timeout_ms = conf.discard_timeout_ms;
    if (_device.empty())
    {
        bool found = false;
        for (const auto& probe : {TTY_PROBE_DEV1, TTY_PROBE_DEV2})
        {
            Debug_printf("Trying %s\n", probe);
            if ((_fd = open(probe, O_RDWR | O_NOCTTY | O_NONBLOCK)) >= 0)
            {
                _device = probe;
                found = true;
                break;
            }
        }

        if (found)
            Debug_printf("Setting up serial port %s\n", _device.c_str());
    }
    else
    {
        Debug_printf("Setting up serial port %s\n", _device.c_str());
        _fd = open(_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    }

    if (_fd < 0)
    {
        Debug_printf("Failed to open serial port, error %d: %s\n", errno, strerror(errno));
        return;
    }

#if defined(__linux__)
    // Enable low latency
    struct serial_struct ss;
    if (ioctl(_fd, TIOCGSERIAL, &ss) == -1)
    {
        Debug_printf("TTY warning: TIOCGSERIAL failed: %d - %s\n", errno, strerror(errno));
    }
    else
    {
        ss.flags |= ASYNC_LOW_LATENCY;
        if (ioctl(_fd, TIOCSSERIAL, &ss) == -1)
            Debug_printf("TTY warning: TIOCSSERIAL failed: %d - %s\n", errno, strerror(errno));
    }
#endif

    // Read in existing settings
    struct termios tios;
    if(tcgetattr(_fd, &tios) != 0)
    {
        Debug_printf("tcgetattr error %d: %s\n", errno, strerror(errno));
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
        return;
    }

    Debug_printf("### TTY initialized ###\n");
    setBaudrate(conf.baud_rate);

    return;
}

void TTYChannel::end()
{
    if (_fd >= 0)
    {
        close(_fd);
        _fd = -1;
        Debug_printf("### TTY stopped ###\n");
    }

    return;
}

void TTYChannel::updateFIFO()
{
    int avail = 0;

    if (ioctl(_fd, FIONREAD, &avail) == -1)
        return;
    if (!avail)
        return;

    size_t old_len = _fifo.size();
    _fifo.resize(old_len + avail);
    int result = ::read(_fd, &_fifo[old_len], avail);
    if (result < 0)
        result = 0;
    _fifo.resize(old_len + result);

    return;
}

timeval timeval_from_ms(const uint32_t millis)
{
  timeval tv;
  tv.tv_sec = millis / 1000;
  tv.tv_usec = (millis - (tv.tv_sec * 1000)) * 1000;
  return tv;
}

size_t TTYChannel::dataOut(const void *buffer, size_t length)
{
    int result;
    int txbytes;
    fd_set writefds;
    timeval timeout_tv(timeval_from_ms(500));

    for (txbytes=0; txbytes<length;)
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
            Debug_printf("TTY write() select error %d: %s\n", errno, strerror(errno));
            break;
        }

        // Timeout
        if (result == 0)
        {
            Debug_println("TTY write() TIMEOUT");
            break;
        }

        if (result > 0)
        {
            // Make sure our file descriptor is in the ready to write list
            if (FD_ISSET(_fd, &writefds))
            {
                // This will write some
                result = ::write(_fd, &((uint8_t *)buffer)[txbytes], length-txbytes);
                // Debug_printf("write: %d\n", result);
                if (result < 1)
                {
                    Debug_printf("TTY write() write error %d: %s\n", errno, strerror(errno));
                    break;
                }

                txbytes += result;
                if (txbytes == length)
                {
                    // TODO is flush missing somewhere else?
                    // wait TTY is writable again before return
                    timeout_tv = timeval_from_ms(1000 + result * 12500 / _baud);
                    select(_fd + 1, NULL, &writefds, NULL, &timeout_tv);
                    // done
                    break;
                }
                if (txbytes < length)
                {
                    timeout_tv = timeval_from_ms(1000 + result * 12500 / _baud);
                    continue;
                }
            }
            // This shouldn't happen, if r > 0 our fd has to be in the list!
            Debug_println("TTY write() unexpected select result");
        }
    }
    return txbytes;
}

void TTYChannel::flushOutput()
{
    tcdrain(_fd);
    return;
}

void TTYChannel::setBaudrate(uint32_t baud)
{
    Debug_printf("TTY set_baudrate: %d\n", baud);

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

#ifdef UNUSED
// FIXME - why does this function exist? Shouldn't the caller use begin()?
void TTYChannel::setPort(std::string device)
{
    Debug_printv("%s", device.c_str());
    _device = device;
    return;
}
#endif /* UNUSED */

std::string TTYChannel::getPort()
{
    return _device;
}

bool TTYChannel::getDTR()
{
    int status;

    if (ioctl(_fd, TIOCMGET, &status) == -1)
        return false;

    return !!(status & TIOCM_DSR);
}

void TTYChannel::setDSR(bool state)
{
    int status;

    if (state == _dtrState)
        return;
    if (ioctl(_fd, TIOCMGET, &status) == -1)
        return;

    _dtrState = state;
    status &= ~TIOCM_DTR;
    if (_dtrState)
        status |= TIOCM_DTR;
    ioctl(_fd, TIOCMSET, &status);
    return;
}

bool TTYChannel::getRTS()
{
    int status;

    if (ioctl(_fd, TIOCMGET, &status) == -1)
        return false;

    return !!(status & TIOCM_CTS);
}

void TTYChannel::setCTS(bool state)
{
    int status;

    if (state == _rtsState)
        return;
    if (ioctl(_fd, TIOCMGET, &status) == -1)
        return;

    _rtsState = state;
    status &= ~TIOCM_RTS;
    if (_rtsState)
        status |= TIOCM_RTS;
    ioctl(_fd, TIOCMSET, &status);
    return;
}

bool TTYChannel::getRI()
{
    int status;

    if (ioctl(_fd, TIOCMGET, &status) == -1)
        return false;

    return !!(status & TIOCM_RI);
}

#endif /* ITS_A_UNIX_SYSTEM_I_KNOW_THIS */
