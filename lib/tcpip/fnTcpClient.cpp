/* Modified version of ESP-Arduino fnTcpClient.cpp/h */

#include "fnTcpClient.h"

#include <cstring>
#include <fcntl.h>
#include <errno.h>

#ifndef ESP_PLATFORM
# if defined(_WIN32)
// this only eliminates compilation errors on Windows
// for non-blocking socket operations FIONBIO must be set
#  define MSG_DONTWAIT 0
# else
#  include <sys/ioctl.h>
#  include <netinet/tcp.h>
# endif

# ifndef MSG_NOSIGNAL
#  if defined(_WIN32)
#   define MSG_NOSIGNAL 0
#  endif
// MSG_NOSIGNAL does not exists on older macOS
#  if defined(__APPLE__) || defined(__MACH__)
#   define USE_SO_NOSIGPIPE
#   define MSG_NOSIGNAL 0
#  endif
# endif
#endif // !ESP_PLATFORM

#include "fnDNS.h"

#include "../../include/debug.h"


#define FNTCP_MAX_WRITE_RETRY (10)
#define FNTCP_SELECT_TIMEOUT_US (1000000)
#define FNTCP_FLUSH_BUFFER_SIZE (1024)

class fnTcpClientSocketHandle
{
private:
    int _sockfd;

public:
    fnTcpClientSocketHandle(int fd) : _sockfd(fd) {}
    ~fnTcpClientSocketHandle() { close(); }

    int fd() { return _sockfd; }
    int close()
    {
        int res = (_sockfd >= 0) ? closesocket(_sockfd) : -1;
        _sockfd = -1;
        return res;
    }
};

fnTcpClient::fnTcpClient(int fd)
{
    _connected = true;
    _clientSocketHandle.reset(new fnTcpClientSocketHandle(fd));
    _rxBuffer.clear();
}

fnTcpClient::~fnTcpClient()
{
    stop();
}

void fnTcpClient::stop()
{
    _clientSocketHandle = nullptr;
    _connected = false;
}

int fnTcpClient::connect(in_addr_t ip, uint16_t port, int32_t timeout)
{
    // Get new socket file descriptor
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        Debug_printf("socket: %d\r\n", compat_getsockerr());
        return 0;
    }

#ifdef USE_SO_NOSIGPIPE
    // set SO_NOSIGPIPE on macOS without MSG_NOSIGNAL
    {
        int set = 1;
        setSocketOption(SO_NOSIGPIPE, (char *)&set, sizeof(int));
    }
#endif

    // Add O_NONBLOCK to our socket file descriptor
#if defined(_WIN32)
    unsigned long on = 1;
    ioctlsocket(sockfd, FIONBIO, &on);
#else
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
#endif

    // Fill a new sockaddr_in struct with our info
    struct sockaddr_in serveraddr;
    memset((char *)&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = ip;
    serveraddr.sin_port = htons(port);

    // Connect to the server
    int res = ::connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    int err = compat_getsockerr();

#if defined(_WIN32)
    if (res < 0 && err != WSAEWOULDBLOCK)
#else
    if (res < 0 && err != EINPROGRESS)
#endif
    {
        Debug_printf("connect on fd %d, errno: %d, \"%s\"\r\n", sockfd, err, compat_sockstrerror(err));
        closesocket(sockfd);
        // re-set errno for errno_to_error()
        compat_setsockerr(err);
        return 0;
    }

    // Wait for the socket to be ready for writing to
    // fdset contains the file descriptor(s) we're checking for readiness
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    // select for error on sockfd too
    fd_set fdseterr;
    FD_ZERO(&fdseterr);
    FD_SET(sockfd, &fdseterr);

    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout - tv.tv_sec*1000) * 1000;

    res = select(sockfd + 1, nullptr, &fdset, &fdseterr, timeout < 0 ? nullptr : &tv);
    // Error result
    if (res < 0)
    {
        err = compat_getsockerr();
        Debug_printf("select on fd %d, errno: %d, \"%s\"\r\n", sockfd, err, compat_sockstrerror(err));
        closesocket(sockfd);
        // re-set errno
        compat_setsockerr(err);
        return 0;
    }
    // Timeout reached
    else if (res == 0)
    {
        Debug_printf("select returned due to timeout %lu ms for fd %d\r\n", timeout, sockfd);
        closesocket(sockfd);
#if defined(_WIN32)
        err = WSAETIMEDOUT;
#else
        err = ETIMEDOUT;
#endif
        // set errno
        compat_setsockerr(err);
        return 0;
    }
    // Success (ready for write) OR error on socket
    else
    {
        int sockerr;
        socklen_t len = (socklen_t)sizeof(int);
        // Store any socket error value in sockerr
        res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&sockerr, &len);
        if (res < 0)
        {
            // Failed to retrieve SO_ERROR
            err = compat_getsockerr();
            Debug_printf("getsockopt on fd %d, errno: %d, \"%s\"\r\n", sockfd, err ,compat_sockstrerror(err));
            closesocket(sockfd);
            // set errno
            compat_setsockerr(err);
            return 0;
        }
        // Retrieved SO_ERROR and found that we have an error condition
        if (sockerr != 0)
        {
            Debug_printf("socket error on fd %d, errno: %d, \"%s\"\r\n", sockfd, sockerr, compat_sockstrerror(sockerr));
            closesocket(sockfd);
            // set errno
            compat_setsockerr(sockerr);
            return 0;
        }
    }

    // Remove the O_NONBLOCK flag
#if defined(_WIN32)
    // cannot use MSG_DONTWAIT on Windows, keep O_NONBLOCK
    // unsigned long off = 0;
    // ioctlsocket(sockfd, FIONBIO, &off);
#else
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK));
#endif
    // Create a socket handle and recieve buffer objects
    _clientSocketHandle.reset(new fnTcpClientSocketHandle(sockfd));
    _rxBuffer.clear();
    _connected = true;

    return 1;
}

int fnTcpClient::connect(const char *host, uint16_t port, int32_t timeout)
{
    in_addr_t ip = get_ip4_addr_by_name(host);
    return connect(ip, port, timeout);
}

// Set both send and receive timeouts on the TCP socket
int fnTcpClient::setTimeout(uint32_t seconds)
{
    //Client::setTimeout(seconds * 1000); // This sets the timeout on the underlying Stream in the WiFiClient code
#if defined(_WIN32)
    DWORD tv = 1000 * seconds;
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
#endif
    if (setSocketOption(SO_RCVTIMEO, (char *)&tv, sizeof(tv)) < 0)
        return -1;
    return setSocketOption(SO_SNDTIMEO, (char *)&tv, sizeof(tv));
}

// Set socket option
int fnTcpClient::setSocketOption(int option, char *value, size_t len)
{
    int res = setsockopt(fd(), SOL_SOCKET, option, value, len);
    if (res < 0)
    {
        Debug_printf("%X : %d\r\n", option, compat_getsockerr());
    }

    return res;
}

// Set TCP option
int fnTcpClient::setOption(int option, int *value)
{
    int res = setsockopt(fd(), IPPROTO_TCP, option, (char *)value, sizeof(int));
    if (res < 0)
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\r\n", fd(), compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
    }

    return res;
}

// Get TCP option
int fnTcpClient::getOption(int option, int *value)
{
    socklen_t size = sizeof(int);
    int res = getsockopt(fd(), IPPROTO_TCP, option, (char *)value, &size);
    if (res < 0)
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\r\n", fd(), compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
    }

    return res;
}

int fnTcpClient::setNoDelay(bool nodelay)
{
    int flag = nodelay;
    return setOption(TCP_NODELAY, &flag);
}

bool fnTcpClient::getNoDelay()
{
    int flag = 0;
    getOption(TCP_NODELAY, &flag);
    return flag;
}

// Returns number of bytes written/sent
size_t fnTcpClient::write(const uint8_t *buf, size_t size)
{
    int socketFileDescriptor = fd();
    if (!_connected || (socketFileDescriptor < 0))
        return 0;

    size_t totalBytesSent = 0;
    size_t bytesRemaining = size;
    int res = 0;
    int retry = FNTCP_MAX_WRITE_RETRY;

    while (retry)
    {
        // Use select to make sure the socket is ready for writing
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(socketFileDescriptor, &fdset);

        struct timeval tv;
        tv.tv_sec = (long)(FNTCP_SELECT_TIMEOUT_US / 1000000UL);
        tv.tv_usec = (long)(FNTCP_SELECT_TIMEOUT_US % 1000000UL);

        retry--;

        // Don't bother retyring if there was an error confirming the socket was ready for writing
        if (select(socketFileDescriptor + 1, nullptr, &fdset, nullptr, &tv) < 0)
            return 0;

        // Go ahead if we got confirmation that the socket is ready for writing
        // (Otherwise we timed-out and should retry the loop)
        if (FD_ISSET(socketFileDescriptor, &fdset))
        {
            res = send(socketFileDescriptor, (char *)buf, bytesRemaining, MSG_DONTWAIT | MSG_NOSIGNAL);
            // We succeeded sending some bytes
            if (res > 0)
            {
                totalBytesSent += res;
                if (totalBytesSent >= size)
                {
                    // Completed successfully - no need to loop again
                    retry = 0;
                }
                else
                {
                    buf += res;
                    bytesRemaining -= res;
                    // Reset our retry count since we've sent some bytes
                    retry = FNTCP_MAX_WRITE_RETRY;
                }
            }
            // We got an error
            else if (res < 0)
            {
                int err = compat_getsockerr();
                Debug_printf("fail on fd %d, errno: %d, \"%s\"\r\n", fd(), err, strerror(err));
                // Give up if this wasn't just a try again error
                if (err != EAGAIN)
                {
                    stop();
                    res = 0;
                    retry = 0;
                }
            }
            // If res == 0 we timed-out: try again
        }
    }
    return totalBytesSent;
}

// Send std::string of data
size_t fnTcpClient::write(const std::string str)
{
    return write((uint8_t *)str.c_str(), str.length());
}

// Send zero-terminated string of data
size_t fnTcpClient::write(const char *buff)
{
    if(buff == nullptr)
        return 0;
    size_t len = strlen(buff);
    return write((uint8_t *)buff, len);
}

// Send just one byte of data
size_t fnTcpClient::write(uint8_t data)
{
    return write(&data, 1);
}

// Fill buffer with read data
int fnTcpClient::read(uint8_t *buf, size_t size)
{
    size_t rlen;

    rlen = std::min(available(), size);
    if (rlen)
    {
        memcpy(buf, _rxBuffer.data(), rlen);
        _rxBuffer.erase(0, rlen);
    }
    return rlen;
}

// Read one byte of data. Return read byte or negative value for error
int fnTcpClient::read()
{
    uint8_t data = 0;
    int res = read(&data, 1);
    if (res < 0)
        return res;
    return data;
}

// Read bytes of data up until the size of our buffer or when we get our terminator
int fnTcpClient::read_until(char terminator, char *buf, size_t size)
{
    if(buf == nullptr || size < 1)
        return 0;

    size_t count = 0;
    while(count < size)
    {
        int c = read();
        if(c < 0 || c == terminator)
            break;

        *buf++ = (char) c;
        count++;
    }
    return count;
}

// Peek at next byte available for reading
int fnTcpClient::peek()
{
    if (_rxBuffer.size())
        return _rxBuffer[0];
    return -1;
}

void fnTcpClient::updateFIFO()
{
    // check if socket is still connected
    if (!connected())
    {
        // connection was closed or it has an error
        return;
    }

#if defined(_WIN32)
    unsigned long count;
    int res = ioctlsocket(fd(), FIONREAD, &count);
    res = res != 0 ? -1 : count;
#else
    int count;
    int res = ioctl(fd(), FIONREAD, &count);
    res = res < 0 ? -1 : count;
#endif

    if (res > 0)
    {
        ssize_t result;

        for (count = res; count; count -= result)
        {
            size_t old_len = _rxBuffer.size();
            _rxBuffer.resize(old_len + count);
            result = recv(fd(), &_rxBuffer[old_len], count, 0);
            if (result < 0)
                result = 0;
            _rxBuffer.resize(old_len + result);
        }

    }

    return;
}

// Return number of bytes available for reading
size_t fnTcpClient::available()
{
    updateFIFO();
    return _rxBuffer.size();
}

// Send all pending data and clear receive buffer
void fnTcpClient::flush()
{
    int res;
    size_t a = available(), toRead = 0;
    if (!a)
        return; // Nothing to flush

    uint8_t *buf = (uint8_t *)malloc(FNTCP_FLUSH_BUFFER_SIZE);
    if (!buf)
        return; // Memory error

    while (a)
    {
        toRead = (a > FNTCP_FLUSH_BUFFER_SIZE) ? FNTCP_FLUSH_BUFFER_SIZE : a;
        res = recv(fd(), (char *)buf, toRead, MSG_DONTWAIT);
        if (res < 0)
        {
            Debug_printf("fail on fd %d, errno: %d, \"%s\"\r\n",
                fd(), compat_getsockerr(), compat_sockstrerror(compat_getsockerr()));
            stop();
            break;
        }
        a -= res;
    }
    free(buf);
}

uint8_t fnTcpClient::connected()
{
    // Check if we're really connected still
    if (_connected)
    {
        uint8_t dummy;
        int res = recv(fd(), (char *)&dummy, 1, MSG_PEEK | MSG_DONTWAIT);

        if (res > 0)
        {
            _connected = true;
        }
        else if (res == 0)
        {
            Debug_printf("fnTcpClient disconnected\r\n");
            _connected = false;
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
#endif
            case ENOENT: // Caused by VFS
                _connected = true;
                break;
#if defined(_WIN32)
            case WSAENETDOWN:
            case WSAENETRESET:
            case WSAESHUTDOWN:
            case WSAECONNABORTED:
            case WSAECONNRESET:
            case WSAETIMEDOUT:
#else
            case ENOTCONN:
            case EPIPE:
            case ECONNRESET:
            case ECONNREFUSED:
            case ECONNABORTED:
#endif
                _connected = false;
                Debug_printf("fnTcpClient disconnected: res %d, errno %d\r\n", res, err);
                break;
            default:
                Debug_printf("fnTcpClient unexpected: res %d, errno %d\r\n", res, err);
                _connected = true;
                break;
            }
        }
    }
    return _connected;
}

in_addr_t fnTcpClient::remoteIP(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getpeername(fd, (struct sockaddr *)&addr, &len);

    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return s->sin_addr.s_addr;
}

uint16_t fnTcpClient::remotePort(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getpeername(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return ntohs(s->sin_port);
}

in_addr_t fnTcpClient::remoteIP() const
{
    return remoteIP(fd());
}

uint16_t fnTcpClient::remotePort() const
{
    return remotePort(fd());
}

in_addr_t fnTcpClient::localIP(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getsockname(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return s->sin_addr.s_addr;
}

uint16_t fnTcpClient::localPort(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getsockname(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return ntohs(s->sin_port);
}

in_addr_t fnTcpClient::localIP() const
{
    return localIP(fd());
}

uint16_t fnTcpClient::localPort() const
{
    return localPort(fd());
}

int fnTcpClient::fd() const
{
    if (_clientSocketHandle == nullptr)
        return -1;
    else
        return _clientSocketHandle->fd();
}
