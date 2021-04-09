/* Modified version of ESP-Arduino fnTcpClient.cpp/h */

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <errno.h>

#include "../../include/debug.h"

#include "fnDNS.h"
#include "fnTcpClient.h"

#define FNTCP_MAX_WRITE_RETRY (10)
#define FNTCP_SELECT_TIMEOUT_US (1000000)
#define FNTCP_FLUSH_BUFFER_SIZE (1024)

class fnTcpClientRxBuffer
{
private:
    size_t _size;
    uint8_t *_buffer;
    size_t _pos;
    size_t _fill;
    int _fd;
    bool _failed;

    size_t r_available()
    {
        if (_fd < 0)
        {
            return 0;
        }
        int count;
        int res = lwip_ioctl(_fd, FIONREAD, &count);
        if (res < 0)
        {
            _failed = true;
            return 0;
        }
        return count;
    }

    // Fill our buffer with data from the given socket descriptor
    // Returns how much data was read
    size_t fillBuffer()
    {
        // Allocate space for the buffer if we haven't already
        if (!_buffer)
        {
            _buffer = (uint8_t *)malloc(_size);
            if (!_buffer)
            {
                Debug_printf("Not enough memory to allocate buffer\n");
                _failed = true;
                return 0;
            }
        }

        if (_fill && _pos == _fill)
        {
            _fill = 0;
            _pos = 0;
        }

        if (!_buffer || _size <= _fill || !r_available())
        {
            return 0;
        }

        // Read the data
        int res = recv(_fd, _buffer + _fill, _size - _fill, MSG_DONTWAIT);
        if (res < 0)
        {
            if (errno != EWOULDBLOCK)
            {
                _failed = true;
            }
            return 0;
        }
        _fill += res;
        return res;
    }

public:
    fnTcpClientRxBuffer(int fd, size_t size = 1436)
        : _size(size), _buffer(NULL), _pos(0), _fill(0), _fd(fd), _failed(false) {}

    ~fnTcpClientRxBuffer() { free(_buffer); }

    bool failed() { return _failed; }

    // Read data and return how many bytes were read
    int read(uint8_t *dst, size_t len)
    {
        // Fail if bad param or we're at the end of our buffer and couldn't get more data
        if (!dst || !len || (_pos == _fill && !fillBuffer()))
        {
            return -1;
        }

        size_t a = _fill - _pos;
        if (len <= a || ((len - a) <= (_size - _fill) && fillBuffer() >= (len - a)))
        {
            if (len == 1)
            {
                *dst = _buffer[_pos];
            }
            else
            {
                memcpy(dst, _buffer + _pos, len);
            }
            _pos += len;
            return len;
        }

        size_t left = len;
        size_t toRead = a;
        uint8_t *buf = dst;
        memcpy(buf, _buffer + _pos, toRead);
        _pos += toRead;
        left -= toRead;
        buf += toRead;
        while (left)
        {
            if (!fillBuffer())
                return len - left;

            a = _fill - _pos;
            toRead = (a > left) ? left : a;
            memcpy(buf, _buffer + _pos, toRead);
            _pos += toRead;
            left -= toRead;
            buf += toRead;
        }
        return len;
    }

    // Return value at current buffer position
    int peek()
    {
        // Return an error if we don't have any more data in our buffer and couldn't get more
        if (_pos == _fill && !fillBuffer())
            return -1;

        return _buffer[_pos];
    }

    size_t available()
    {
        return _fill - _pos + r_available();
    }
};

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
        int res = (_sockfd >= 0) ? ::close(_sockfd) : -1;
        _sockfd = -1;
        return res;
    }
};

fnTcpClient::fnTcpClient(int fd)
{
    _connected = true;
    _clientSocketHandle.reset(new fnTcpClientSocketHandle(fd));
    _rxBuffer.reset(new fnTcpClientRxBuffer(fd));
}

fnTcpClient::~fnTcpClient()
{
    stop();
}

void fnTcpClient::stop()
{
    _clientSocketHandle = nullptr;
    _rxBuffer = nullptr;
    _connected = false;
}

int fnTcpClient::connect(in_addr_t ip, uint16_t port, int32_t timeout)
{
    // Get new socket file descriptor
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        Debug_printf("socket: %d\n", errno);
        return 0;
    }
    // Add O_NONBLOCK to our socket file descriptor
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    // Fill a new sockaddr_in struct with our info
    struct sockaddr_in serveraddr;
    memset((char *)&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = ip;
    serveraddr.sin_port = htons(port);

    // Connect to the server
    int res = lwip_connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
    if (res < 0 && errno != EINPROGRESS)
    {
        Debug_printf("connect on fd %d, errno: %d, \"%s\"\n", sockfd, errno, strerror(errno));
        ::close(sockfd);
        return 0;
    }

    // Wait for the socket to be ready for writing to
    // fdset contains the file descriptor(s) we're checking for readiness
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);

    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout - tv.tv_sec*1000) * 1000;

    res = select(sockfd + 1, nullptr, &fdset, nullptr, timeout < 0 ? nullptr : &tv);
    // Error result
    if (res < 0)
    {
        Debug_printf("select on fd %d, errno: %d, \"%s\"\n", sockfd, errno, strerror(errno));
        ::close(sockfd);
        return 0;
    }
    // Timeout reached
    else if (res == 0)
    {
        Debug_printf("select returned due to timeout %d ms for fd %d\n", timeout, sockfd);
        ::close(sockfd);
        return 0;
    }
    // Success
    else
    {
        int sockerr;
        socklen_t len = (socklen_t)sizeof(int);
        // Store any socket error value in sockerr
        res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sockerr, &len);
        if (res < 0)
        {
            // Failed to retrieve SO_ERROR
            Debug_printf("getsockopt on fd %d, errno: %d, \"%s\"\n", sockfd, errno, strerror(errno));
            ::close(sockfd);
            return 0;
        }
        // Retrieved SO_ERROR and found that we have an error condition
        if (sockerr != 0)
        {
            Debug_printf("socket error on fd %d, errno: %d, \"%s\"\n", sockfd, sockerr, strerror(sockerr));
            ::close(sockfd);
            return 0;
        }
    }
    // Remove the O_NONBLOCK flag
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK));
    // Create a socket handle and recieve buffer objects
    _clientSocketHandle.reset(new fnTcpClientSocketHandle(sockfd));
    _rxBuffer.reset(new fnTcpClientRxBuffer(sockfd));
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
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setSocketOption(SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)) < 0)
        return -1;
    return setSocketOption(SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
}

// Set socket option
int fnTcpClient::setSocketOption(int option, char *value, size_t len)
{
    int res = setsockopt(fd(), SOL_SOCKET, option, value, len);
    if (res < 0)
    {
        Debug_printf("%X : %d\n", option, errno);
    }

    return res;
}

// Set TCP option
int fnTcpClient::setOption(int option, int *value)
{
    int res = setsockopt(fd(), IPPROTO_TCP, option, (char *)value, sizeof(int));
    if (res < 0)
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
    }

    return res;
}

// Get TCP option
int fnTcpClient::getOption(int option, int *value)
{
    size_t size = sizeof(int);
    int res = getsockopt(fd(), IPPROTO_TCP, option, (char *)value, &size);
    if (res < 0)
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
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
            res = send(socketFileDescriptor, (void *)buf, bytesRemaining, MSG_DONTWAIT);
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
                Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
                // Give up if this wasn't just a try again error
                if (errno != EAGAIN)
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
    int res = -1;
    res = _rxBuffer->read(buf, size);
    if (_rxBuffer->failed())
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
        stop();
    }
    return res;
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
    int res = _rxBuffer->peek();
    if (_rxBuffer->failed())
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
        stop();
    }
    return res;
}

// Return number of bytes available for reading
int fnTcpClient::available()
{
    if (!_rxBuffer)
        return 0;

    int res = _rxBuffer->available();
    if (_rxBuffer->failed())
    {
        Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
        stop();
    }
    return res;
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
        res = recv(fd(), buf, toRead, MSG_DONTWAIT);
        if (res < 0)
        {
            Debug_printf("fail on fd %d, errno: %d, \"%s\"\n", fd(), errno, strerror(errno));
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
        int res = recv(fd(), &dummy, 1, MSG_PEEK | MSG_DONTWAIT);

        if (res > 0)
        {
            _connected = true;
        }
        else if (res == 0)
        {
            Debug_printf("fnTcpClient disconnected\n");
            _connected = false;
        }
        else
        {
            switch (errno)
            {
            case EWOULDBLOCK:
            case ENOENT: // Caused by VFS
                _connected = true;
                break;
            case ENOTCONN:
            case EPIPE:
            case ECONNRESET:
            case ECONNREFUSED:
            case ECONNABORTED:
                _connected = false;
                Debug_printf("fnTcpClient disconnected: res %d, errno %d\n", res, errno);
                break;
            default:
                Debug_printf("fnTcpClient unexpected: res %d, errno %d\n", res, errno);
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

int fnTcpClient::close()
{
    int res = (_clientSocketHandle != nullptr) ? _clientSocketHandle->close() : -1;
    stop();
    return res;
}
