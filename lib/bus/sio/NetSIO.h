#ifndef NETSIO_H
#define NETSIO_H

#include "IOChannel.h"
#include "fnDNS.h"
#include <sys/time.h>

#define NETSIO_DATA_BYTE        0x01
#define NETSIO_DATA_BLOCK       0x02

#define NETSIO_FILL_BUFFER      0x02
#define NETSIO_TRANSMITT_BUFFER 0x03

#define NETSIO_DATA_BYTE_SYNC   0x09

#define NETSIO_COMMAND_OFF      0x10
#define NETSIO_COMMAND_ON       0x11
#define NETSIO_COMMAND_OFF_SYNC 0x18
#define NETSIO_MOTOR_OFF        0x20
#define NETSIO_MOTOR_ON         0x21
#define NETSIO_PROCEED_OFF      0x30
#define NETSIO_PROCEED_ON       0x31
#define NETSIO_INTERRUPT_OFF    0x40
#define NETSIO_INTERRUPT_ON     0x41

#define NETSIO_SPEED_CHANGE     0x80
#define NETSIO_SYNC_RESPONSE    0x81
#define NETSIO_BUS_IDLE         0x88

#define NETSIO_DEVICE_DISCONNECT 0xC0
#define NETSIO_DEVICE_CONNECT   0xC1
#define NETSIO_PING_REQUEST     0xC2
#define NETSIO_PING_RESPONSE    0xC3
#define NETSIO_ALIVE_REQUEST    0xC4
#define NETSIO_ALIVE_RESPONSE   0xC5
#define NETSIO_CREDIT_STATUS    0xC6
#define NETSIO_CREDIT_UPDATE    0xC7

#define NETSIO_WARM_RESET       0xFE
#define NETSIO_COLD_RESET       0xFF


#define NETSIO_EMPTY_SYNC       0x00
#define NETSIO_ACK_SYNC         0x01

#define NETSIO_PORT             9997

class NetSIO : public IOChannel
{
private:
    std::string _host;
    in_addr_t _ip;
    uint16_t _port;

    uint32_t _baud;
    uint32_t _baud_peer;
    int _fd;
    bool _initialized;
    bool _command_asserted;
    bool _motor_asserted;

    int _sync_request_num;  // 0..255 sync request sequence number, -1 if sync is not requested
    uint8_t _sync_ack_byte; // ACK byte to send with sync response
    int _sync_write_size;   // 0 .. no SIO write (from computer), > 0 .. expected bytes written

    // serial port error counter
    int _errcount;
    uint64_t _resume_time;
    uint64_t _alive_time;    // when last message was received
    uint64_t _alive_request; // when last ALIVE request was sent
    // flow control
    int _credit;

    void handle_write_sync(uint8_t c);

protected:
    void suspend(int ms=5000);
    bool resume_test();
    bool keep_alive();

    int handle_netsio();
    static timeval timeval_from_ms(const uint32_t millis);

    bool wait_sock_readable(uint32_t timeout_ms);
    bool wait_for_data(uint32_t timeout_ms);
    bool wait_for_credit(int needed);

    bool wait_sock_writable(uint32_t timeout_ms);
    ssize_t write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=500);

    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t size) override;

public:
    NetSIO();
    virtual ~NetSIO();
    void begin(std::string host, int port, int baud);
    void end() override;

    void setBaudrate(uint32_t baud);
    uint32_t getBaudrate();

    bool commandAsserted();
    bool motorAsserted();
    void setProceed(bool level);
    void setInterrupt(bool level);

    void flushOutput() override;
    int ping(int count=4, int interval_ms=1000, int timeout_ms=500, bool fast=true);

    void setWriteSize(int write_size);
    void setHost(std::string host, int port);
    void setSyncAckByte(int ack_byte);
    void sendEmptySync();
    ssize_t sendSyncResponse(uint8_t response_type, uint8_t ack_byte=0,
                             uint16_t sync_write_size=0);
    void busIdle(uint16_t ms);
};

#endif // NETSIO_H
