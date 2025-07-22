#ifndef BECKERSOCKET_H
#define BECKERSOCKET_H

#include "SerialInterface.h"

#include <sys/time.h>
#include <vector>

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
    char _host[64]; // TODO change to std::string
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

#ifdef NOT_SUBCLASS
    size_t do_read(uint8_t *buffer, size_t size);
    ssize_t do_write(const uint8_t *buffer, size_t size);

    ssize_t read_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=BECKER_IOWAIT_MS);
    ssize_t write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=BECKER_IOWAIT_MS);
#endif /* NOT_SUBCLASS */

    bool wait_sock_readable(uint32_t timeout_ms, bool listener=false);
    bool wait_sock_writable(uint32_t timeout_ms);

    void update_fifo() override;
#ifdef NOT_SUBCLASS
    size_t si_recv(void *buffer, size_t size) override {
        return do_read((uint8_t *) buffer, size); }
    size_t si_send(const void *buffer, size_t size) override {
        return do_write((uint8_t *) buffer, size); }
#else
    size_t si_send(const void *buffer, size_t size) override;
#endif /* NOT_SUBCLASS */
    
public:
    BeckerSocket();
    virtual ~BeckerSocket();
    void begin(int baud);
    void end() override;

    // mimic UARTManager, baudrate is not used by BeckerSocket
    void setBaudrate(uint32_t baud) override { _baud = baud; }
    uint32_t getBaudrate() override { return _baud; }

#ifdef NOT_SUBCLASS
    size_t available() override;
    void discardInput() override;
#endif /* NOT_SUBCLASS */
    void flush() override;

    // specific to BeckerSocket
    void set_host(const char *host, int port);
    const char* get_host(int &port);

    inline BeckerState getState() const { return _state; }
    void setState(BeckerState state) { _state = state; }
};



#endif // BECKERSOCKET_H
