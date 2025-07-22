#ifdef BUILD_COCO

#include "BeckerSocket.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "compat_string.h"
#include <sys/time.h>
#include <unistd.h> // write(), read(), close()
#include <errno.h> // Error integer and strerror() function
#include <fcntl.h> // Contains file controls like O_RDWR

#ifndef ESP_PLATFORM

#if !defined(_WIN32)
#  include <sys/ioctl.h>
#  include <netinet/tcp.h>
#endif

#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#  if defined(__APPLE__) || defined(__MACH__)
// MSG_NOSIGNAL does not exists on older macOS, use SO_NOSIGPIPE
#    define USE_SO_NOSIGPIPE
#  endif
#endif

#endif // !ESP_PLATFORM

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnWiFi.h"


#define DW_DEFAULT_BAUD         57600


// Constructor
BeckerSocket::BeckerSocket() :
    _host{0},
    _ip(IPADDR_NONE),
    _port(BECKER_DEFAULT_PORT),
    _baud(DW_DEFAULT_BAUD),     // not used by Becker
    _listening(true),
    _fd(-1),
    _listen_fd(-1),
#ifdef NOT_SUBCLASS
    _state(&BeckerStopped::getInstance()),
#else
    _state(BeckerStopped),
#endif /* NOT_SUBCLASS */
    _errcount(0)
{}

BeckerSocket::~BeckerSocket()
{
    end();
}

void BeckerSocket::begin(int baud)
{
#ifdef NOT_SUBCLASS
    if (_state != &BeckerStopped::getInstance())
        end();
#else
    if (_state != BeckerStopped)
        end();
#endif /* NOT_SUBCLASS */

    _baud = baud;

    // listen or connect
    start_connection();
}

void BeckerSocket::end()
{
    // close sockets
    if (_fd >= 0)
    {
        shutdown(_fd, 0);
        closesocket(_fd);
        _fd  = -1;
    }
    if (_listen_fd >= 0)
    {
        closesocket(_listen_fd);
        _listen_fd  = -1;
        Debug_printf("### BeckerSocket stopped ###\n");
    }

    // wait a while, otherwise wifi may turn off too quickly (during shutdown)
    fnSystem.delay(50);

#ifdef NOT_SUBCLASS
    setState(BeckerStopped::getInstance());
#else
    setState(BeckerStopped);
#endif /* NOT_SUBCLASS */
}

/* Returns number of bytes available in receive buffer or -1 on error
*/
size_t BeckerSocket::available()
{
    poll_connection(1);

    // only in connected state
#ifdef NOT_SUBCLASS
    if (_state != &BeckerConnected::getInstance())
        return 0;
#else
    if (_state != BeckerConnected)
        return 0;
#endif /* NOT_SUBCLASS */

    // check if socket is still connected
    if (!connected())
    {
        // connection was closed or it has an error
        suspend_on_disconnect();
        return 0;
    }

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

/* Discards anything in the input buffer
*/
void BeckerSocket::discardInput()
{
    // only in connected state
#ifdef NOT_SUBCLASS
    if (_state != &BeckerConnected::getInstance())
        return;
#else
    if (_state != BeckerConnected)
        return;
#endif /* NOT_SUBCLASS */

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
void BeckerSocket::flush()
{
    // only in connected state
#ifdef NOT_SUBCLASS
    if (_state != &BeckerConnected::getInstance())
        return;
#else
    if (_state != BeckerConnected)
        return;
#endif /* NOT_SUBCLASS */

    wait_sock_writable(250);
}

// specific to BeckerSocket
void BeckerSocket::set_host(const char *host, int port)
{
    if (host != nullptr)
        strlcpy(_host, host, sizeof(_host));
    else
        _host[0] = 0;

    _port = port;
}

const char* BeckerSocket::get_host(int &port)
{
    port = _port;
    return _host;
}

void BeckerSocket::start_connection()
{
    if (_listening)
        listen_for_connection();
    else
        make_connection();
}

void BeckerSocket::listen_for_connection()
{

    // Wait for WiFi
    if (!fnWiFi.connected())
    {
        Debug_println("BeckerSocket: No WiFi!");
        // suspend for 0.5 or 2 sec, depending on _errcount
        suspend(1000, 5000, 5);
        return;
    }

    Debug_printf("Setting up BeckerSocket: listening on %s:%d\n", _host, _port);

    // Create listening socket
    _listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (_listen_fd < 0)
    {
        Debug_printf("BeckerSocket: failed to create socket: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    // Set socket option
    int enable = 1;
#if defined(_WIN32)
    if (setsockopt(_listen_fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &enable, sizeof(enable)) != 0)
#else
        if (setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0)
#endif
        {
            Debug_printf("BeckerSocket: setsockopt failed: %d - %s\n",
                         compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
            closesocket(_listen_fd);
            _listen_fd = -1;
            suspend(BECKER_SUSPEND_MS);
            return;
        }

    // Local address to listen on
    if (_host[0] == '\0' || !strcmp(_host, "*"))
    {
        _ip = IPADDR_ANY;
    }
    else
    {
        _ip = get_ip4_addr_by_name(_host);
        if (_ip == IPADDR_NONE)
        {
            Debug_println("BeckerSocket: failed to resolve host name");
            closesocket(_listen_fd);
            _listen_fd = -1;
            suspend(BECKER_SUSPEND_MS);
            return;
        }
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = _ip;
    addr.sin_port = htons(_port);

    // Bind to listening address
    if (bind(_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        Debug_printf("BeckerSocket: bind failed: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    // Listen for incoming connection
    if (listen(_listen_fd, 1) != 0)
    {
        Debug_printf("BeckerSocket: listen failed: %d  %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    // Set socket non-blocking
    if (!compat_socket_set_nonblocking(_listen_fd))
    {
        Debug_printf("BeckerSocket: failed to set non-blocking mode: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    // Finally setup
    _errcount = 0; // used by suspend()
#ifdef NOT_SUBCLASS
    setState(BeckerWaitConn::getInstance());
#else
    setState(BeckerWaitConn);
#endif /* NOT_SUBCLASS */
    Debug_printf("### BeckerSocket accepting connections ###\n");
}

void BeckerSocket::make_connection()
{

    // Wait for WiFi
    if (!fnWiFi.connected())
    {
        Debug_println("BeckerSocket: No WiFi!");
        // suspend for 0.5 or 2 sec, depending on _errcount
        suspend(1000, 5000, 5);
		return;
	}

    Debug_printf("Setting up BeckerSocket: connecting to %s:%d\n", _host, _port);

    // Create connection socket
    _fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (_fd < 0)
    {
        Debug_printf("BeckerSocket: failed to create socket: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BECKER_SUSPEND_MS);
		return;
	}

#ifdef USE_SO_NOSIGPIPE
    // Set NOSIGPIPE socket option (old macOS)
    int enable = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_NOSIGPIPE, (char *)&enable, sizeof(enable)) < 0)
    {
        Debug_printf("BeckerSocket: setsockopt failed: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BECKER_SUSPEND_MS);
        return;
    }
#endif

    // Set socket non-blocking
    if (!compat_socket_set_nonblocking(_fd))
    {
        Debug_printf("BeckerSocket: failed to set non-blocking mode: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    // Remote address
    if (_host[0] == '\0')
    {
        _ip = IPADDR_LOOPBACK;
    }
    else
    {
        _ip = get_ip4_addr_by_name(_host);
        if (_ip == IPADDR_NONE)
        {
            Debug_println("BeckerSocket: failed to resolve host name");
            suspend(BECKER_SUSPEND_MS);
            return;
        }
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = _ip;
    addr.sin_port = htons(_port);

    // Connect to remote address
    int res = connect(_fd, (struct sockaddr *)&addr, sizeof(addr));
    int err = compat_getsockerr();

#if defined(_WIN32)
    if (res < 0 && err != WSAEWOULDBLOCK)
#else
    if (res < 0 && err != EINPROGRESS)
#endif
    {
        Debug_printf("BeckerSocket: connect failed: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    if (!wait_sock_writable(BECKER_CONNECT_TMOUT))
    {
        Debug_printf("BeckerSocket: socket not ready\n");
        suspend(BECKER_SUSPEND_MS);
        return;
    }

    // Finally setup
    _errcount = 0;
#ifdef NOT_SUBCLASS
    setState(BeckerConnected::getInstance());
#else
    setState(BeckerConnected);
#endif /* NOT_SUBCLASS */
    Debug_print("### BeckerSocket connected ###\n");
}


bool BeckerSocket::accept_pending_connection(int ms)
{
    // if listening socket has new connection accept it
    return(wait_sock_readable(ms, true) && accept_connection());
}


bool BeckerSocket::accept_connection()
{
    struct sockaddr_in addr;
    int as = sizeof(struct sockaddr_in);

    // Accept connection
    _fd = accept(_listen_fd, (struct sockaddr *)&addr, (socklen_t *)&as);
    if (_fd < 0)
    {
        Debug_printf("BeckerSocket: accept failed: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        return false;
    }
    Debug_printf("BeckerSocket: connection from: %s\r\n", inet_ntoa(addr.sin_addr));

    // Set socket options
    int val = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&val, sizeof(val)) < 0)
    {
        Debug_printf("BeckerSocket warning: failed to set KEEPALIVE on socket\n");
    }
    if (setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)) < 0)
    {
        Debug_printf("BeckerSocket warning: failed to set NODELAY on socket\n");
    }

    // Set socket non-blocking
    if (!compat_socket_set_nonblocking(_fd))
    {
        Debug_printf("BeckerSocket: failed to set non-blocking connection: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        shutdown(_fd, 0);
        closesocket(_fd);
        _fd = -1;
        return false;
    }

    // We are connected !
    Debug_print("### BeckerSocket connected ###\n");
#ifdef NOT_SUBCLASS
    setState(BeckerConnected::getInstance());
#else
    setState(BeckerConnected);
#endif /* NOT_SUBCLASS */
    return true;
}

void BeckerSocket::suspend(int short_ms, int long_ms, int threshold)
{
    if (_fd >= 0)
    {
        closesocket(_fd);
        _fd  = -1;
    }
    _errcount++;
    _suspend_time = fnSystem.millis();
    _suspend_period = short_ms;
    if (threshold > 0 && _errcount > threshold && long_ms > 0)
        _suspend_period = long_ms;
    Debug_printf("Suspending BeckerSocket for %d ms\n", _suspend_period);
#ifdef NOT_SUBCLASS
    setState(BeckerSuspended::getInstance());
#else
    setState(BeckerSuspended);
#endif /* NOT_SUBCLASS */
}

void BeckerSocket::suspend_on_disconnect()
{
    if (_listening && _listen_fd >=0)
    {
        if (_fd >= 0)
        {
            closesocket(_fd);
            _fd = -1;
        }
        // go directly into waiting for connection state
#ifdef NOT_SUBCLASS
        setState(BeckerWaitConn::getInstance());
#else
        setState(BeckerWaitConn);
#endif /* NOT_SUBCLASS */
    }
    else
    {
        // wait before reconnecting
        suspend(BECKER_SUSPEND_MS);
    }
}

bool BeckerSocket::resume()
{
    // Debug_print("Resuming BeckerSocket\n");
    if (_listening)
    {
        if (_listen_fd >= 0)
        {
            // go directly into waiting for connection state
#ifdef NOT_SUBCLASS
            setState(BeckerWaitConn::getInstance());
#else
            setState(BeckerWaitConn);
#endif /* NOT_SUBCLASS */
            return true;
        }

    }
    // listen or connect
    start_connection();
#ifdef NOT_SUBCLASS
    return (_state != &BeckerSuspended::getInstance());
#else
    return (_state != BeckerSuspended);
#endif /* NOT_SUBCLASS */
}

bool BeckerSocket::suspend_period_expired()
{
    return (fnSystem.millis() - _suspend_time > _suspend_period);
}

bool BeckerSocket::connected()
{
    uint8_t dummy;
    bool con = false;
    int res = recv(_fd, (char *)&dummy, 1, MSG_PEEK);
    if (res > 0)
    {
        con = true;
    }
    else if (res == 0)
    {
        Debug_print("### BeckerSocket disconnected ###\n");
    }
    else
    {
        int err = compat_getsockerr();
        switch (err)
        {
#if defined(_WIN32)
        case WSAEWOULDBLOCK:
#else
        case EWOULDBLOCK:
        case ENOENT: // Caused by VFS
#endif
            con = true;
            break;
        default:
            Debug_printf("BeckerSocket: connection error: %d - %s\n",
                compat_getsockerr(), compat_sockstrerror(err));
            break;
        }
    }
    return con;
}

bool BeckerSocket::poll_connection(int ms)
{
    switch (_state)
    {
    case BeckerSuspended:
        if (!suspend_period_expired())
        {
            // still suspended
#ifndef ESP_PLATFORM
            fnSystem.delay(ms); // be nice to CPU
#endif
            return false;
        }
        // resume
        return resume();

    case BeckerWaitConn:
        return accept_pending_connection(ms); // true if new connection was accepted

    case BeckerStopped:
#ifndef ESP_PLATFORM
        fnSystem.delay(ms); // be nice to CPU
#endif
        return false;

    case BeckerConnected:
        break;
    }

    if (wait_sock_readable(ms) && !connected())
    {
        // connection was closed or it has an error
        suspend_on_disconnect();
    }

    return false;
}

timeval BeckerSocket::timeval_from_ms(const uint32_t millis)
{
  timeval tv;
  tv.tv_sec = millis / 1000;
  tv.tv_usec = (millis - (tv.tv_sec * 1000)) * 1000;
  return tv;
}

size_t BeckerSocket::do_read(uint8_t *buffer, size_t size)
{
    int result;
    int to_recv;
    int rxbytes = 0;

    while (rxbytes < size)
    {
        to_recv = size - rxbytes;
        result = read_sock(buffer+rxbytes, to_recv);
        if (result > 0)
            rxbytes += result;
        else // result <= 0 for disconnected or write error
            break;
    }
    return rxbytes;
}

ssize_t BeckerSocket::do_write(const uint8_t *buffer, size_t size)
{
    int result;
    int to_send;
    int txbytes = 0;

    while (txbytes < size)
    {
        to_send = size - txbytes;
        result = write_sock(buffer+txbytes, to_send);
        if (result > 0)
            txbytes += result;
        else if (result < 0) // write error
            break;
    }
    return txbytes;
}

ssize_t BeckerSocket::read_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    if (!wait_sock_readable(timeout_ms))
    {
        Debug_printf("BeckerSocket: read_sock() TIMEOUT\n");
        return -1;
    }

    ssize_t result = recv(_fd, (char *)buffer, size, 0);
    if (result < 0)
    {
        Debug_printf("BeckerSocket: read_sock() error: %d - %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend_on_disconnect();
    }
    else if (result == 0)
    {
        Debug_printf("BeckerSocket disconnected\n");
        suspend_on_disconnect();
    }
    return result;
}

ssize_t BeckerSocket::write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    if (!wait_sock_writable(timeout_ms))
    {
        int err = compat_getsockerr();
#if defined(_WIN32)
        if (err == WSAETIMEDOUT)
#else
        if (err == ETIMEDOUT)
#endif
        {
            Debug_printf("BeckerSocket: write_sock() TIMEOUT\n");
        }
        else
        {
            suspend_on_disconnect();
        }
        return -1;
    }

    ssize_t result = send(_fd, (char *)buffer, size, 0);
    if (result < 0)
    {
        Debug_printf("BeckerSocket write_sock() error %d: %s\n",
            compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend_on_disconnect();
    }
    return result;
}

bool BeckerSocket::wait_sock_readable(uint32_t timeout_ms, bool listener)
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
            Debug_printf("BeckerSocket: wait_sock_readable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
            return false;

        // this shouldn't happen, if result > 0 our fd has to be in the list!
        if (!FD_ISSET(fd, &readfds))
        {
            Debug_println("BeckerSocket: wait_sock_readable() unexpected select result");
            return false;
        }
        break;
    }
    return true;
}

bool BeckerSocket::wait_sock_writable(uint32_t timeout_ms)
{
    timeval timeout_tv;
    fd_set writefds;
    fd_set errfds;
    int result;

    for(;;)
    {
        timeout_tv = timeval_from_ms(timeout_ms);
        // select for write
        FD_ZERO(&writefds);
        FD_SET(_fd, &writefds);
        // select for error too
        FD_ZERO(&errfds);
        FD_SET(_fd, &errfds);

        result = select(_fd + 1, nullptr, &writefds, &errfds, &timeout_tv);

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
            Debug_printf("BeckerSocket wait_sock_writable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
        {
#if defined(_WIN32)
            int err = WSAETIMEDOUT;
#else
            int err = ETIMEDOUT;
#endif
            // set errno
            compat_setsockerr(err);
            return false;
        }
        // Check for error on socket
        {
            int sockerr;
            socklen_t len = (socklen_t)sizeof(int);
            // Store any socket error value in sockerr
            int res = getsockopt(_fd, SOL_SOCKET, SO_ERROR, (char *)&sockerr, &len);
            if (res < 0)
            {
                // Failed to retrieve SO_ERROR
                int err = compat_getsockerr();
                Debug_printf("getsockopt on fd %d, errno: %d - %s\n", _fd, err ,compat_sockstrerror(err));
                return false;
            }
            // Retrieved SO_ERROR and found that we have an error condition
            if (sockerr != 0)
            {
                Debug_printf("socket error on fd %d, errno: %d - %s\n", _fd, sockerr, compat_sockstrerror(sockerr));
                // set errno
                compat_setsockerr(sockerr);
                return false;
            }
        }
        //Debug_print("socket is ready for write\n");
        break;
    }
    return true;
}

void BeckerSocket::update_fifo()
{
    return;
}

//
// Becker state handlers
//

#ifdef NOT_SUBCLASS
// Stopped state

bool BeckerStopped::poll(BeckerSocket *port, int ms)
{
#ifndef ESP_PLATFORM
    fnSystem.delay(ms); // be nice to CPU
#endif
    return false;
}

size_t BeckerStopped::read(BeckerSocket *port, uint8_t *buffer, size_t size)
{
    // read timeout
    fnSystem.delay(BECKER_IOWAIT_MS);
    return 0;
}

ssize_t BeckerStopped::write(BeckerSocket *port, const uint8_t *buffer, size_t size)
{
    // write timeout
    fnSystem.delay(BECKER_IOWAIT_MS);
    return 0;
}

// Suspended state

bool BeckerSuspended::poll(BeckerSocket *port, int ms)
{
    if (!port->suspend_period_expired())
    {
        // still suspended
#ifndef ESP_PLATFORM
        fnSystem.delay(ms); // be nice to CPU
#endif
        return false;
    }
    // resume
    return port->resume();
}

size_t BeckerSuspended::read(BeckerSocket *port, uint8_t *buffer, size_t size)
{
    if (!port->suspend_period_expired())
    {
        // still suspended, read timeout
        fnSystem.delay(BECKER_IOWAIT_MS);
        return 0;
    }
    // resume
    if (port->resume())
    {
        // connection was resumed, we can proceed with read
        return port->getState()->read(port, buffer, size);
    }
    return 0;
}

ssize_t BeckerSuspended::write(BeckerSocket *port, const uint8_t *buffer, size_t size)
{
    if (!port->suspend_period_expired())
    {
        // still suspended, read timeout
        fnSystem.delay(BECKER_IOWAIT_MS);
        return 0;
    }
    // resume
    if (port->resume())
    {
        // connection was resumed, we can proceed with write
        return port->getState()->write(port, buffer, size);
    }
    return 0;
}

// Waiting for connection

bool BeckerWaitConn::poll(BeckerSocket *port, int ms)
{
    return port->accept_pending_connection(ms); // true if new connection was accepted
}

size_t BeckerWaitConn::read(BeckerSocket *port, uint8_t *buffer, size_t size)
{
    if (port->accept_pending_connection(BECKER_IOWAIT_MS))
    {
        // connection was accepted, we can proceed with read
        return port->getState()->read(port, buffer, size);
    }
    return 0;
}

ssize_t BeckerWaitConn::write(BeckerSocket *port, const uint8_t *buffer, size_t size)
{
    if (port->accept_pending_connection(BECKER_IOWAIT_MS))
    {
        // connection was accepted, we can proceed with write
        return port->getState()->write(port, buffer, size);
    }
    return 0;
}

// Connected

bool BeckerConnected::poll(BeckerSocket *port, int ms)
{
    return port->poll_connection(ms);
}

size_t BeckerConnected::read(BeckerSocket *port, uint8_t *buffer, size_t size)
{
    return port->do_read(buffer, size);
}

ssize_t BeckerConnected::write(BeckerSocket *port, const uint8_t *buffer, size_t size)
{
    return port->do_write(buffer, size);
}
#endif /* NOT_SUBCLASS */

#endif // BUILD_COCO
