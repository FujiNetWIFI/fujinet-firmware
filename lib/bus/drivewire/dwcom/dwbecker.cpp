#ifdef BUILD_COCO

#include "dwbecker.h"

/*
 * TODO - TCP listen,accept,read,write code here
 */



#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "compat_string.h"
#include <sys/time.h>
#include <unistd.h> // write(), read(), close()
#include <errno.h> // Error integer and strerror() function
#include <fcntl.h> // Contains file controls like O_RDWR

#ifndef ESP_PLATFORM
# if !defined(_WIN32)
#  include <sys/ioctl.h>
#  include <netinet/tcp.h>
# endif
#endif

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"


#define DW_DEFAULT_BAUD         57600


// Constructor
BeckerPort::BeckerPort() :
    _host{0},
    _ip(IPADDR_NONE),
    _port(BECKER_DEFAULT_PORT),
    _baud(DW_DEFAULT_BAUD),     // not used by Becker
    _fd(-1),
    _listen_fd(-1),
    _initialized(false),
    _connected(false),
    _errcount(0)
{}

BeckerPort::~BeckerPort()
{
    end();
}

void BeckerPort::begin(int baud)
{
    if (_initialized) 
    {
        end();
    }

    _resume_time = 0;

    // Wait for WiFi
    int suspend_ms = _errcount < 5 ? 400 : 2000;
    if (!fnWiFi.connected())
    {
        Debug_println("BeckerPort: No WiFi!");
        _errcount++;
        suspend(suspend_ms);
		return;
	}

    suspend_ms = _errcount < 5 ? 1000 : 5000;
    Debug_printf("Setting up BeckerPort (%s:%d)\n", _host, _port);
    _listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    if (_listen_fd < 0)
    {
        Debug_printf("Failed to create BeckerPort socket: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
		return;
	}
    
    _ip = get_ip4_addr_by_name(_host);
    if (_ip == IPADDR_NONE)
    {
        Debug_println("Failed to resolve BeckerPort host name");
        _errcount++;
        suspend(suspend_ms);
		return;
    }

    // Set remote IP address (no real connection is created for UDP socket)
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = _ip;
    addr.sin_port = htons(_port);

    int enable = 1;
#if defined(_WIN32)
    if (setsockopt(_listen_fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &enable, sizeof(enable)) != 0)
#else
    if (setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
#endif
    {
        Debug_printf("BeckerPort::begin - setsockopt failed: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
        return;
    }

    if (bind(_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        Debug_printf("BeckerPort::begin - bind failed: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
        return;
    }

    if (listen(_listen_fd, 1) < 0)
    {
        Debug_printf("BeckerPort::begin - listen failed: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
        return;
    }

    // non-blocking mode
#if defined(_WIN32)
    unsigned long on = 1;
    if (ioctlsocket(_listen_fd, FIONBIO, &on) < 0)
#else
    if (fcntl(_listen_fd, F_SETFL, fcntl(_listen_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
#endif
    {
        Debug_printf("BeckerPort::begin - failed to set non-blocking mode: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        _errcount++;
        suspend(suspend_ms);
        return;
    }

    Debug_printf("### BeckerPort initialized ###\n");
    // Set initialized.
    _initialized = true;
    _errcount = 0;
    set_baudrate(baud);
}

void BeckerPort::end()
{
    if (_fd >= 0)
    {
        closesocket(_fd);
        _fd  = -1;
    }
    if (_listen_fd >= 0)
    {
        closesocket(_listen_fd);
        _listen_fd  = -1;
        Debug_printf("### BeckerPort stopped ###\n");
    }
    _initialized = false;
    _connected = false;
    fnSystem.delay(50); // wait a while, otherwise wifi may turn off too quickly (during shutdown)
}

bool BeckerPort::poll(int ms)
{
    if (_initialized)
    {
        if(_connected)
        {
        #ifdef ESP_PLATFORM
            return false;
        #else
            return wait_sock_readable(ms); // be nice to CPU
        #endif
        }

        // already initialized but no client connected
        if (wait_sock_readable(ms, true))
        {
            if (accept_connection())
            {
                _connected = true;
                return true;
            }
            // TODO
            // _errcount++;
            // suspend(suspend_ms);
            return false;
        }
    }

#ifndef ESP_PLATFORM
    fnSystem.delay(ms); // be nice to CPU
#endif
    return false;
}

bool BeckerPort::accept_connection()
{
    struct sockaddr_in addr;
    int as = sizeof(struct sockaddr_in);
    _fd = ::accept(_listen_fd, (struct sockaddr *)&addr, (socklen_t *)&as);
    if (_fd < 0)
    {
        Debug_printf("BeckerPort - accept failed: %d, \"%s\"\n", 
        compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        return false;
    }
    Debug_printf("BeckerPort - connection from: %s\r\n", inet_ntoa(addr.sin_addr));

    int val = 1;
    setsockopt(_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&val, sizeof(val));
    setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

    // non-blocking mode
#if defined(_WIN32)
    unsigned long on = 1;
    if (ioctlsocket(_fd, FIONBIO, &on) < 0)
#else
    if (fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL, 0) | O_NONBLOCK) < 0)
        
#endif
    {
        Debug_printf("BeckerPort - failed to set non-blocking connection: %d, \"%s\"\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        return false;
    }
    return true;
}


void BeckerPort::suspend(int ms)
{
    Debug_printf("Suspending BeckerPort for %d ms\n", ms);
    _resume_time = fnSystem.millis() + ms;
    end();
}

timeval BeckerPort::timeval_from_ms(const uint32_t millis)
{
  timeval tv;
  tv.tv_sec = millis / 1000;
  tv.tv_usec = (millis - (tv.tv_sec * 1000)) * 1000;
  return tv;
}

bool BeckerPort::wait_sock_readable(uint32_t timeout_ms, bool listener)
{
    timeval timeout_tv;
    fd_set readfds;
    int result;
    int fd = listener ? _listen_fd : _fd;

    for(;;)
    {
        // Setup a select call to block for socket data or a timeout
        timeout_tv = timeval_from_ms(timeout_ms);
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        result = select(fd + 1, &readfds, nullptr, nullptr, &timeout_tv);

        // select error
        if (result < 0)
        {
            int err = compat_getsockerr();
#if defined(_WIN32)
            if (err == WSAEINTR)
#else
            if (err == EINTR) 
#endif
            {
                // TODO adjust timeout_tv
                continue;
            }
            Debug_printf("BeckerPort wait_sock_readable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
            return false;

        // this shouldn't happen, if result > 0 our fd has to be in the list!
        if (!FD_ISSET(fd, &readfds))
        {
            Debug_println("BeckerPort wait_sock_readable() unexpected select result");
            return false;
        }
        break;
    }
    return true;
}

ssize_t BeckerPort::read_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    if (!wait_sock_readable(timeout_ms))
    {
        Debug_println("BeckerPort read_sock() TIMEOUT");
        return -1;
    }

    ssize_t result = recv(_fd, (char *)buffer, size, 0);
    if (result < 0)
    {
        Debug_printf("BeckerPort read_sock() recv error %d: %s\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
    }
    return result;
}

bool BeckerPort::wait_sock_writable(uint32_t timeout_ms)
{
    timeval timeout_tv;
    fd_set writefds;
    int result;

    for(;;)
    {
        timeout_tv = timeval_from_ms(timeout_ms);
        FD_ZERO(&writefds);
        FD_SET(_fd, &writefds);
        result = select(_fd + 1, nullptr, &writefds, nullptr, &timeout_tv);

        // select error
        if (result < 0) 
        {
            int err = compat_getsockerr();
#if defined(_WIN32)
            if (err == WSAEINTR)
#else
            if (err == EINTR) 
#endif
            {
                // TODO adjust timeout_tv
                continue;
            }
            Debug_printf("BeckerPort wait_sock_writable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
            return false;

        // this shouldn't happen, if result > 0 our fd has to be in the list!
        if (!FD_ISSET(_fd, &writefds)) 
        {
            Debug_println("BeckerPort wait_sock_writable() unexpected select result");
            return false;
        }
        break;
    }
    return true;
}

ssize_t BeckerPort::write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    if (!wait_sock_writable(timeout_ms))
    {
        Debug_println("BeckerPort write_sock() TIMEOUT");
        return -1;
    }

    ssize_t result = send(_fd, (char *)buffer, size, 0);
    if (result < 0)
    {
        Debug_printf("BeckerPort write_sock() send error %d: %s\n", 
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
    }
    return result;
}

/* Discards anything in the input buffer
*/
void BeckerPort::flush_input()
{
    if (!_connected)
        return;

    // waste all input data
    uint8_t rxbuf[256];
    int avail;
    while ((avail = available()) > 0)
    {
        recv(_fd, (char *)rxbuf, avail > sizeof(rxbuf) ? sizeof(rxbuf) : avail, 0);
    }
}

/* Clears input buffer and flushes out transmit buffer waiting at most
   waiting MAX_FLUSH_WAIT_TICKS until all sends are completed
*/
void BeckerPort::flush()
{
    if (!_connected)
        return;
    wait_sock_writable(250);
}

/* Returns number of bytes available in receive buffer or -1 on error
*/
int BeckerPort::available()
{
#if defined(_WIN32)
    unsigned long count;
    int res = ioctlsocket(_fd, FIONREAD, &count);
    res = res != 0 ? -1 : count;
#else
    int count;
    int res = ioctl(_fd, FIONREAD, &count);
    res = res < 0 ? -1 : count;
#endif
    return res;
}

/* Changes baud rate
*/
void BeckerPort::set_baudrate(uint32_t baud)
{
    // not used by Becker
    _baud = baud;
}

uint32_t BeckerPort::get_baudrate()
{
    return _baud;
}

/* Returns a single byte from the incoming stream
*/
int BeckerPort::read(void)
{
    if (!_connected)
        return -1;
    uint8_t c = 0;
    ssize_t result = read_sock(&c, sizeof(c));
    return (result > 0) ? c : -1;
}

/* Since the underlying Stream calls this Read() multiple times to get more than one
*  character for ReadBytes(), we override with a single call to uart_read_bytes
*/
size_t BeckerPort::read(uint8_t *buffer, size_t size)
{
    if (!_connected)
        return 0;

    int result;
    int to_recv;
    int rxbytes = 0;

    while (rxbytes < size)
    {
        to_recv = size - rxbytes;
        result = read_sock(buffer+rxbytes, to_recv);
        if (result > 0)
            rxbytes += result;
        else if (result < 0)
            break;
    }
    return rxbytes;
}

/* write single byte via BeckerPort */
ssize_t BeckerPort::write(uint8_t c)
{
    if (!_connected)
        return 0;

    ssize_t result = write_sock(&c, sizeof(c));

    return (result > 0) ? 1 : 0; // amount of data bytes written
}

ssize_t BeckerPort::write(const uint8_t *buffer, size_t size)
{
    if (!_connected)
        return 0;

    int result;
    int to_send;
    int txbytes = 0;

    while (txbytes < size)
    {
        to_send = size - txbytes;
        result = write_sock(buffer+txbytes, to_send);
        if (result > 0)
            txbytes += result;
        else if (result < 0)
            break;
    }
    return txbytes;
}

// specific to BeckerPort
void BeckerPort::set_host(const char *host, int port)
{
    if (host != nullptr)
        strlcpy(_host, host, sizeof(_host));
    else
        _host[0] = 0;

    _port = port;
}

const char* BeckerPort::get_host(int &port)
{
    port = _port;
    return _host;
}

#endif // BUILD_COCO
