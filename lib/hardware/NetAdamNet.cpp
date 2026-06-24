#include "NetAdamNet.h"

#ifndef ESP_PLATFORM // PC-only transport

#include "compat_inet.h"
#include "fnDNS.h"
#include "../../include/debug.h"

#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

// Don't hammer connect() while the emulator isn't up yet.
#define NETADAMNET_RECONNECT_THROTTLE_MS 1000
// Only matters when a byte is genuinely missing; reset per byte received.
#define NETADAMNET_READ_TIMEOUT_MS 1000

NetAdamNet::~NetAdamNet()
{
    end();
}

void NetAdamNet::begin(const std::string &host, int port)
{
    end();

    _host = host;
    _port = (uint16_t)port;

    // Tolerate network latency before declaring a byte missing.
    read_timeout_ms = NETADAMNET_READ_TIMEOUT_MS;
    discard_timeout_ms = 0;

    Debug_printf("NetAdamNet: AdamNet-over-IP, connecting to %s:%u\n", _host.c_str(), _port);
    ensure_connected();
}

bool NetAdamNet::ensure_connected()
{
    if (_fd >= 0)
        return true;

    if (_host.empty() || _port == 0)
        return false;

    // Throttle reconnect attempts.
    uint64_t now = GET_TIMESTAMP();
    if (now - _last_connect_attempt < (uint64_t)NETADAMNET_RECONNECT_THROTTLE_MS * 1000)
        return false;
    _last_connect_attempt = now;

    // Resolve the host once and cache it; ADAMEm may be offline for a long time.
    if (_ip == IPADDR_NONE)
    {
        _ip = get_ip4_addr_by_name(_host.c_str());
        if (_ip == IPADDR_NONE)
        {
            if (!_connect_warned)
            {
                Debug_printf("NetAdamNet: cannot resolve %s; waiting quietly\n", _host.c_str());
                _connect_warned = true;
            }
            return false;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);
    addr.sin_addr.s_addr = _ip;

    // Blocking connect: fails fast (ECONNREFUSED) if the emulator isn't listening.
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        // Probably not running yet; log once, then retry silently.
        if (!_connect_warned)
        {
            Debug_printf("NetAdamNet: %s:%u not reachable yet; will keep trying quietly for ADAMEm\n",
                         _host.c_str(), _port);
            _connect_warned = true;
        }
        closesocket(fd);
        return false;
    }

    // AdamNet handshakes are latency-sensitive; disable Nagle.
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));

    if (!compat_socket_set_nonblocking(fd))
        Debug_printf("NetAdamNet: warning: could not set non-blocking\n");

    _fd = fd;
    _connect_warned = false; // reset so the next offline period reports once
    Debug_printf("NetAdamNet: connected to %s:%u\n", _host.c_str(), _port);
    return true;
}

void NetAdamNet::updateFIFO()
{
    if (!ensure_connected())
        return;

    uint8_t buf[2048];
    ssize_t r = recv(_fd, (char *)buf, sizeof(buf), 0);

    if (r > 0)
    {
        _fifo.append((const char *)buf, (size_t)r);
        return;
    }

    if (r == 0)
    {
        // Peer closed the connection.
        Debug_printf("NetAdamNet: peer closed connection\n");
        closesocket(_fd);
        _fd = -1;
        return;
    }

    // r < 0
    int err = compat_getsockerr();
#if defined(_WIN32)
    if (err == WSAEWOULDBLOCK)
        return;
#else
    if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
        return;
#endif
    Debug_printf("NetAdamNet: recv error: %s\n", compat_sockstrerror(err));
    closesocket(_fd);
    _fd = -1;
}

size_t NetAdamNet::dataOut(const void *buffer, size_t length)
{
    // Local echo: feed our TX back into RX so drain_echo()/wait_for_idle() work.
    _fifo.append((const char *)buffer, length);

    if (!ensure_connected())
        return length;

    const uint8_t *p = (const uint8_t *)buffer;
    size_t sent = 0;
    while (sent < length)
    {
        ssize_t w = send(_fd, (const char *)(p + sent), length - sent, 0);
        if (w > 0)
        {
            sent += (size_t)w;
            continue;
        }

        int err = compat_getsockerr();
#if defined(_WIN32)
        bool again = (err == WSAEWOULDBLOCK);
#else
        bool again = (err == EAGAIN || err == EWOULDBLOCK || err == EINTR);
#endif
        if (again)
        {
            // Wait briefly for the socket to drain.
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(_fd, &wfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 500000; // 500 ms
            if (select(_fd + 1, NULL, &wfds, NULL, &tv) <= 0)
            {
                Debug_printf("NetAdamNet: send timeout/err\n");
                break;
            }
            continue;
        }

        Debug_printf("NetAdamNet: send error: %s\n", compat_sockstrerror(err));
        closesocket(_fd);
        _fd = -1;
        break;
    }

    return length;
}

void NetAdamNet::poll(int ms)
{
    if (_fd >= 0)
    {
        // Block until data arrives so the idle bus doesn't busy-loop.
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        select(_fd + 1, &rfds, NULL, NULL, &tv);
    }
    else
    {
        // Not connected; nap so we don't spin.
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

void NetAdamNet::flushOutput()
{
    // TCP_NODELAY is set, so writes go out immediately; nothing to flush.
}

void NetAdamNet::end()
{
    if (_fd >= 0)
    {
        closesocket(_fd);
        _fd = -1;
    }
    _fifo.clear();
}

#endif // !ESP_PLATFORM
