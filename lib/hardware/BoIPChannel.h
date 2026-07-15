#ifndef BOIPCHANNEL_H
#define BOIPCHANNEL_H

#include "IOChannel.h"
#include "fnDNS.h"

#define BOIP_DEFAULT_PORT     65504
#define BOIP_IOWAIT_MS        500
#define BOIP_CONNECT_TMOUT    2000
#define BOIP_SUSPEND_MS       5000

enum BoIPState {
    BoIPStopped,
    BoIPWaitConn,
    BoIPConnected,
    BoIPSuspended,
};

struct BoIPConfig
{
    std::string host;
    uint16_t port = BOIP_DEFAULT_PORT;
    bool listening = true;
    bool local_echo = false;
    bool non_blocking = false;
    bool no_delay = true;
    double read_timeout_ms = BOIP_IOWAIT_MS;
    double discard_timeout_ms = IOCHANNEL_DEFAULT_TIMEOUT;

    BoIPConfig& hostName(std::string h) {
        host = h; return *this;
    }
    BoIPConfig& portNum(uint16_t p) {
        port = p; return *this;
    }
    BoIPConfig& server() {
        listening = true; return *this;
    }
    BoIPConfig& client() {
        listening = false; return *this;
    }
    BoIPConfig& localEcho(bool e = true) {
        local_echo = e; return *this;
    }
    // Non-blocking updateFIFO(); caller must throttle idle CPU itself via poll().
    BoIPConfig& nonBlocking(bool nb = true) {
        non_blocking = nb; return *this;
    }
    // TCP_NODELAY: disable Nagle for tiny request/response packets. On by default.
    BoIPConfig& noDelay(bool nd = true) {
        no_delay = nd; return *this;
    }
    BoIPConfig& readTimeout(double millis) {
        read_timeout_ms = millis; return *this;
    }
    BoIPConfig& discardTimeout(double millis) {
        discard_timeout_ms = millis; return *this;
    }
};

class BoIPChannel : public IOChannel
{
private:
    std::string _host;
    in_addr_t _ip;
    uint16_t _port;

    // is waiting for connection (listening) or connecting to?
    bool _listening;
    bool _local_echo;
    bool _non_blocking;
    bool _no_delay;

    // file descriptors
    int _fd;
    int _listen_fd;

    // state machine handlers for poll(), read() and write()
    BoIPState _state;

    // error counter
    int _errcount;
#ifdef ESP_PLATFORM
    unsigned long _suspend_time;
#else
    uint64_t _suspend_time;
#endif
    int _suspend_period;

protected:
    void start_connection();
    void listen_for_connection();
    void make_connection();
    bool accept_connection();

    void suspend(int short_ms, int long_ms=0, int threshold=0);
    void suspend_on_disconnect();
    bool resume();
    bool suspend_period_expired();

    bool accept_pending_connection(int ms);

    bool connected();
    bool poll_connection(int ms);

    static timeval timeval_from_ms(const uint32_t millis);

    bool wait_sock_readable(uint32_t timeout_ms, bool listener=false);
    bool wait_sock_writable(uint32_t timeout_ms);

    void updateFIFO() override;
    size_t dataOut(const void *buffer, size_t size) override;

public:
    BoIPChannel();
    virtual ~BoIPChannel();
    void begin(const BoIPConfig& conf);
    void end() override;

    void flushOutput() override;

    void setHost(std::string host, int port);
    void poll(int ms);
};

#endif // BOIPCHANNEL_H
