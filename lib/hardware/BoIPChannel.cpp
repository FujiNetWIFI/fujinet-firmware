#include "BoIPChannel.h"
#include "fnSystem.h"
#include "fnWiFi.h"
#include "compat_string.h"

#include "../../include/debug.h"

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

#define DW_DEFAULT_BAUD         57600

#if defined(_WIN32)
#define DW_CONNECT_ERR WSAEWOULDBLOCK
#define DW_EINTR WSAEINTR
#define DW_ETIMEDOUT WSAETIMEDOUT
#define DW_REUSEADDR SO_EXCLUSIVEADDRUSE
#else
#define DW_CONNECT_ERR EINPROGRESS
#define DW_EINTR EINTR
#define DW_ETIMEDOUT ETIMEDOUT
#define DW_REUSEADDR SO_REUSEADDR
#endif

// Constructor
BoIPChannel::BoIPChannel() :
    _host(""),
    _ip(IPADDR_NONE),
    _port(BOIP_DEFAULT_PORT),
    _listening(true),
    _fd(-1),
    _listen_fd(-1),
    _state(BoIPStopped),
    _errcount(0)
{}

BoIPChannel::~BoIPChannel()
{
    end();
}

void BoIPChannel::begin(const BoIPConfig& conf)
{
    if (_state != BoIPStopped)
        end();

    _host = conf.host;
    _port = conf.port;
    read_timeout_ms = conf.read_timeout_ms;
    discard_timeout_ms = conf.discard_timeout_ms;
    _local_echo = conf.local_echo;

    _listening = conf.listening;
    // listen or connect
    start_connection();
}

void BoIPChannel::end()
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
        Debug_printf("### BoIPChannel stopped ###\n");
    }

    // wait a while, otherwise wifi may turn off too quickly (during shutdown)
    fnSystem.delay(50);

    _state = BoIPStopped;
}

/* Clears input buffer and flushes out transmit buffer waiting at most
   waiting MAX_FLUSH_WAIT_TICKS until all sends are completed
*/
void BoIPChannel::flushOutput()
{
    // only in connected state
    if (_state != BoIPConnected)
        return;

    wait_sock_writable(250);
}

void BoIPChannel::start_connection()
{
    if (_listening)
        listen_for_connection();
    else
        make_connection();
}

void BoIPChannel::listen_for_connection()
{
    // Wait for WiFi
    if (!fnWiFi.connected())
    {
        Debug_println("BoIPChannel: No WiFi!");
        // suspend for 0.5 or 2 sec, depending on _errcount
        suspend(1000, 5000, 5);
        return;
    }

    Debug_printf("Setting up BoIPChannel: listening on %s:%d\n", _host.c_str(), _port);

    // Create listening socket
    _listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (_listen_fd < 0)
    {
        Debug_printf("BoIPChannel: failed to create socket: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Set socket option
    int enable = 1;
    if (setsockopt(_listen_fd, SOL_SOCKET, DW_REUSEADDR, (char *) &enable,
                   sizeof(enable)) != 0)
    {
        Debug_printf("BoIPChannel: setsockopt failed: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Local address to listen on
    if (_host.empty() || _host == "*")
    {
        _ip = IPADDR_ANY;
    }
    else
    {
        _ip = get_ip4_addr_by_name(_host.c_str());
        if (_ip == IPADDR_NONE)
        {
            Debug_println("BoIPChannel: failed to resolve host name");
            closesocket(_listen_fd);
            _listen_fd = -1;
            suspend(BOIP_SUSPEND_MS);
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
        Debug_printf("BoIPChannel: bind failed: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Listen for incoming connection
    if (listen(_listen_fd, 1) != 0)
    {
        Debug_printf("BoIPChannel: listen failed: %d  %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Set socket non-blocking
    if (!compat_socket_set_nonblocking(_listen_fd))
    {
        Debug_printf("BoIPChannel: failed to set non-blocking mode: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        closesocket(_listen_fd);
        _listen_fd = -1;
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Finally setup
    _errcount = 0; // used by suspend()
    _state = BoIPWaitConn;
    Debug_printf("### BoIPChannel accepting connections ###\n");
}

void BoIPChannel::make_connection()
{
    // Wait for WiFi
    if (!fnWiFi.connected())
    {
        Debug_println("BoIPChannel: No WiFi!");
        // suspend for 0.5 or 2 sec, depending on _errcount
        suspend(1000, 5000, 5);
        return;
    }

    Debug_printf("Setting up BoIPChannel: connecting to %s:%d\n", _host.c_str(), _port);

    // Create connection socket
    _fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (_fd < 0)
    {
        Debug_printf("BoIPChannel: failed to create socket: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BOIP_SUSPEND_MS);
        return;
    }

#ifdef USE_SO_NOSIGPIPE
    // Set NOSIGPIPE socket option (old macOS)
    int enable = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_NOSIGPIPE, (char *)&enable, sizeof(enable)) < 0)
    {
        Debug_printf("BoIPChannel: setsockopt failed: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BOIP_SUSPEND_MS);
        return;
    }
#endif

    // Set socket non-blocking
    if (!compat_socket_set_nonblocking(_fd))
    {
        Debug_printf("BoIPChannel: failed to set non-blocking mode: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Remote address
    if (_host.empty())
    {
        _ip = IPADDR_LOOPBACK;
    }
    else
    {
        _ip = get_ip4_addr_by_name(_host.c_str());
        if (_ip == IPADDR_NONE)
        {
            Debug_println("BoIPChannel: failed to resolve host name");
            suspend(BOIP_SUSPEND_MS);
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

    if (res < 0 && err != DW_CONNECT_ERR)
    {
        Debug_printf("BoIPChannel: connect failed: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    if (!wait_sock_writable(BOIP_CONNECT_TMOUT))
    {
        Debug_printf("BoIPChannel: socket not ready\n");
        suspend(BOIP_SUSPEND_MS);
        return;
    }

    // Finally setup
    _errcount = 0;
    _state = BoIPConnected;
    Debug_print("### BoIPChannel connected ###\n");
}

bool BoIPChannel::accept_pending_connection(int ms)
{
    // if listening socket has new connection accept it
    return(wait_sock_readable(ms, true) && accept_connection());
}

bool BoIPChannel::accept_connection()
{
    struct sockaddr_in addr;
    int as = sizeof(struct sockaddr_in);

    // Accept connection
    _fd = accept(_listen_fd, (struct sockaddr *)&addr, (socklen_t *)&as);
    if (_fd < 0)
    {
        Debug_printf("BoIPChannel: accept failed: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        return false;
    }
    Debug_printf("BoIPChannel: connection from: %s\r\n", inet_ntoa(addr.sin_addr));

    // Set socket options
    int val = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&val, sizeof(val)) < 0)
    {
        Debug_printf("BoIPChannel warning: failed to set KEEPALIVE on socket\n");
    }
    if (setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)) < 0)
    {
        Debug_printf("BoIPChannel warning: failed to set NODELAY on socket\n");
    }

    // Set socket non-blocking
    if (!compat_socket_set_nonblocking(_fd))
    {
        Debug_printf("BoIPChannel: failed to set non-blocking connection: %d - %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        shutdown(_fd, 0);
        closesocket(_fd);
        _fd = -1;
        return false;
    }

    // We are connected !
    Debug_print("### BoIPChannel connected ###\n");
    _state = BoIPConnected;
    return true;
}

void BoIPChannel::suspend(int short_ms, int long_ms, int threshold)
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
    Debug_printf("Suspending BoIPChannel for %d ms\n", _suspend_period);
    _state = BoIPSuspended;
}

void BoIPChannel::suspend_on_disconnect()
{
    if (_listening && _listen_fd >=0)
    {
        if (_fd >= 0)
        {
            closesocket(_fd);
            _fd = -1;
        }
        // go directly into waiting for connection state
        _state = BoIPWaitConn;
    }
    else
    {
        // wait before reconnecting
        suspend(BOIP_SUSPEND_MS);
    }
}

bool BoIPChannel::resume()
{
    // Debug_print("Resuming BoIPChannel\n");
    if (_listening)
    {
        if (_listen_fd >= 0)
        {
            // go directly into waiting for connection state
            _state = BoIPWaitConn;
            return true;
        }

    }
    // listen or connect
    start_connection();
    return (_state != BoIPSuspended);
}

bool BoIPChannel::suspend_period_expired()
{
    return (fnSystem.millis() - _suspend_time > _suspend_period);
}

bool BoIPChannel::connected()
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
        Debug_print("### BoIPChannel disconnected ###\n");
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
            Debug_printf("BoIPChannel: connection error: %d - %s\n",
                         compat_getsockerr(), compat_sockstrerror(err));
            break;
        }
    }
    return con;
}

bool BoIPChannel::poll_connection(int ms)
{
    switch (_state)
    {
    case BoIPSuspended:
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

    case BoIPWaitConn:
        return accept_pending_connection(ms); // true if new connection was accepted

    case BoIPStopped:
#ifndef ESP_PLATFORM
        fnSystem.delay(ms); // be nice to CPU
#endif
        return false;

    case BoIPConnected:
        break;
    }

    if (wait_sock_readable(ms) && !connected())
    {
        // connection was closed or it has an error
        suspend_on_disconnect();
    }

    return false;
}

timeval BoIPChannel::timeval_from_ms(const uint32_t millis)
{
    timeval tv;
    tv.tv_sec = millis / 1000;
    tv.tv_usec = (millis - (tv.tv_sec * 1000)) * 1000;
    return tv;
}

bool BoIPChannel::wait_sock_readable(uint32_t timeout_ms, bool listener)
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
            if (err == DW_EINTR)
            {
                // TODO adjust timeout_tv
                continue;
            }
            Debug_printf("BoIPChannel: wait_sock_readable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
            return false;

        // this shouldn't happen, if result > 0 our fd has to be in the list!
        if (!FD_ISSET(fd, &readfds))
        {
            Debug_println("BoIPChannel: wait_sock_readable() unexpected select result");
            return false;
        }
        break;
    }
    return true;
}

bool BoIPChannel::wait_sock_writable(uint32_t timeout_ms)
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
            if (err == DW_EINTR)
            {
                // TODO adjust timeout_tv
                continue;
            }
            Debug_printf("BoIPChannel wait_sock_writable() select error %d: %s\n", err, compat_sockstrerror(err));
            return false;
        }

        // select timeout
        if (result == 0)
        {
            int err = DW_ETIMEDOUT;
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

void BoIPChannel::updateFIFO()
{
    poll_connection(1);

    // only in connected state
    if (_state != BoIPConnected)
        return;

    // check if socket is still connected
    if (!connected())
    {
        // connection was closed or it has an error
        suspend_on_disconnect();
        return;
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

    if (res > 0)
    {
        ssize_t result;

        for (count = res; count; count -= result)
        {
            size_t old_len = _fifo.size();
            _fifo.resize(old_len + count);
            result = recv(_fd, &_fifo[old_len], count, 0);
            if (result < 0)
                result = 0;
            _fifo.resize(old_len + result);
        }

    }

    return;
}

size_t BoIPChannel::dataOut(const void *buffer, size_t size)
{
    // FIXME - we're echoing everything back just so we can discard it later
    if (_local_echo)
        _fifo.append((const char *)buffer, size);

    uint32_t timeout_ms=BOIP_IOWAIT_MS;

    if (!wait_sock_writable(timeout_ms))
    {
        int err = compat_getsockerr();
        if (err == DW_ETIMEDOUT)
        {
            Debug_printf("BoIPChannel: write_sock() TIMEOUT\n");
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
        Debug_printf("BoIPChannel write_sock() error %d: %s\n",
                     compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
        suspend_on_disconnect();
    }
    return result;
}

void BoIPChannel::setHost(std::string host, int port)
{
    _host = host;
    _port = port;
}

void BoIPChannel::poll(int ms)
{
    if (_state == BoIPConnected)
        wait_sock_readable(ms);
    else
        fnSystem.delay(ms);
}
