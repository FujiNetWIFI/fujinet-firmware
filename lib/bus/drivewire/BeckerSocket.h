#ifndef BECKERSOCKET_H
#define BECKERSOCKET_H

#include "SerialInterface.h"
#include "fnDNS.h"

#define BECKER_DEFAULT_PORT     65504
#define BECKER_IOWAIT_MS        500
#define BECKER_CONNECT_TMOUT    2000
#define BECKER_SUSPEND_MS       5000

enum BeckerState {
    BeckerStopped,
    BeckerWaitConn,
    BeckerConnected,
    BeckerSuspended,
};    

class BeckerSocket : public SerialInterface
{
private:
    std::string _host;
    in_addr_t _ip;
    uint16_t _port;

    uint32_t _baud;     // not used by Becker

    // is waiting for connection (listening) or connecting to?
    bool _listening;

    // file descriptors
    int _fd;
    int _listen_fd;

    // state machine handlers for poll(), read() and write()
    BeckerState _state;

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

    void update_fifo() override;
    size_t si_send(const void *buffer, size_t size) override;
    
public:
    BeckerSocket();
    virtual ~BeckerSocket();
    void begin(std::string host, int baud);
    void end() override;

    // mimic UARTManager, baudrate is not used by BeckerSocket
    void setBaudrate(uint32_t baud) override { _baud = baud; }
    uint32_t getBaudrate() override { return _baud; }

    void flush() override;

    inline BeckerState getState() const { return _state; }
    void setState(BeckerState state) { _state = state; }
};



#endif // BECKERSOCKET_H
