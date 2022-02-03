/* Modified version of ESP-Arduino WiFiClient.cpp/h */

#ifndef _FN_TCPCLIENT_H_
#define _FN_TCPCLIENT_H_

#include <memory>


class fnTcpClientSocketHandle;
class fnTcpClientRxBuffer;

class fnTcpClient
{
protected:
    std::shared_ptr<fnTcpClientRxBuffer> _rxBuffer;
    std::shared_ptr<fnTcpClientSocketHandle> _clientSocketHandle;
    bool _connected = false;

public:
    fnTcpClient() {};
    fnTcpClient(int fd);
    ~fnTcpClient();

    void stop();
    int close();

    int connect(const char *host, uint16_t port, int32_t timeout = -1);
    int connect(in_addr_t addr, uint16_t port, int32_t timeout = -1);

    size_t write(uint8_t data);
    size_t write(const uint8_t *buf, size_t size);
    size_t write(const char *buff);
    size_t write(const std::string str);

    int read();
    int read(uint8_t *buf, size_t size);
    int read_until(char terminator, char *buf, size_t size);

    int available();
    int peek();
    void flush();
    uint8_t connected();

    operator bool() { return connected(); }

    int setSocketOption(int option, char* value, size_t len);
    int setOption(int option, int *value);
    int getOption(int option, int *value);
    int setTimeout(uint32_t seconds);
    int setNoDelay(bool nodelay);
    bool getNoDelay();

    in_addr_t remoteIP() const;
    in_addr_t remoteIP(int fd) const;
    uint16_t remotePort() const;
    uint16_t remotePort(int fd) const;
    in_addr_t localIP() const;
    in_addr_t localIP(int fd) const;
    uint16_t localPort() const;
    uint16_t localPort(int fd) const;

    int fd() const;
};

#endif // _FN_TCPCLIENT_H_
