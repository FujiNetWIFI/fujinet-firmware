#ifndef DWBECKER_H
#define DWBECKER_H

#include <sys/time.h>
#include "dwport.h"
#include "fnDNS.h"


#define BECKER_DEFAULT_PORT     9997


class BeckerPort : public DwPort
{
private:
    char _host[64];
    in_addr_t _ip;
    uint16_t _port;

    uint32_t _baud;     // not used by Becker
    int _fd;
    int _listen_fd;
    bool _initialized;
    bool _connected;

    // error counter
    int _errcount;
    uint64_t _resume_time;

protected:
    void suspend(int ms=5000);
    bool accept_connection();

    static timeval timeval_from_ms(const uint32_t millis);

    bool wait_sock_readable(uint32_t timeout_ms, bool listener=false);
    ssize_t read_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=250);

    bool wait_sock_writable(uint32_t timeout_ms);
    ssize_t write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=250);

public:
    BeckerPort();
    virtual ~BeckerPort();
    virtual void begin(int baud) override;
    virtual void end() override;
    virtual bool poll(int ms) override;

    virtual void set_baudrate(uint32_t baud) override;
    virtual uint32_t get_baudrate() override;

    virtual int available() override;
    virtual void flush() override;
    virtual void flush_input() override;

    // read single byte
    virtual int read() override;
    // read bytes into buffer
    virtual size_t read(uint8_t *buffer, size_t size) override;

    // write single byte
    virtual ssize_t write(uint8_t b) override;
    // write buffer
    virtual ssize_t write(const uint8_t *buffer, size_t size) override;

    // specific to BeckerPort
    void set_host(const char *host, int port);
    const char* get_host(int &port);
};

#endif // DWBECKER_H
