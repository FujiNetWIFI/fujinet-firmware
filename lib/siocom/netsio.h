#ifndef NETSIO_H
#define NETSIO_H

#include "sioport.h"
#include "fnDNS.h"

class NetSioPort : public SioPort
{
private:
    char _host[64];
    in_addr_t _ip;
    uint16_t _port;

    uint32_t _baud;
    uint32_t _baud_peer;
    int _fd;
    bool _initialized;
    bool _command_asserted;
    bool _motor_asserted;

    uint8_t _rxbuf[1024];
    int _rxhead;
    int _rxtail;
    bool _rxfull;

    int _sync_request_num;  // 0..255 sync request sequence number, -1 if sync is not requested
    uint8_t _sync_ack_byte; // ACK byte to send with sync response
    int _sync_write_size;   // 0 .. no SIO write (from computer), > 0 .. expected bytes written

    // serial port error counter
    int _errcount;
    unsigned long _resume_time;
    unsigned long _alive_time;
    unsigned long _alive_response;

protected:
    void suspend(int ms=5000);
    bool resume_test();
    bool keep_alive();

    int handle_netsio();
    static timeval timeval_from_ms(const uint32_t millis);

    bool wait_sock_readable(uint32_t timeout_ms);
    bool wait_for_data(uint32_t timeout_ms);

    bool wait_sock_writable(uint32_t timeout_ms);
    ssize_t write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=500);

    bool rxbuffer_empty();
    bool rxbuffer_put(uint8_t b);
    int rxbuffer_get();
    int  rxbuffer_available();
    void rxbuffer_flush();

public:
    NetSioPort();
    virtual ~NetSioPort();
    virtual void begin(int baud) override;
    virtual void end() override;

    virtual void set_baudrate(uint32_t baud) override;
    virtual uint32_t get_baudrate() override;

    virtual bool command_asserted() override;
    virtual bool motor_asserted() override;
    virtual void set_proceed(bool level) override;
    virtual void set_interrupt(bool level) override;

    virtual int available() override;
    virtual void flush() override;
    virtual void flush_input() override;

    // read single byte
    virtual int read() override;
    // read bytes into buffer
    virtual size_t read(uint8_t *buffer, size_t length) override;

    // write single byte
    virtual ssize_t write(uint8_t b) override;
    // write buffer
    virtual ssize_t write(const uint8_t *buffer, size_t size) override;

    // specific to NetSioPort
    void set_host(const char *host, int port);
    const char* get_host(int &port);
    int ping(int count=4, int interval_ms=1000, int timeout_ms=500, bool fast=true);

    void set_sync_ack_byte(int ack_byte);
    void set_sync_write_size(int write_size);
};

#endif // NETSIO_H
