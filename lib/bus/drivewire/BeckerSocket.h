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

#ifdef UNUSED
class BeckerSocket;

class BeckerState
{
public:
    virtual bool poll(BeckerSocket *port, int ms) = 0;
    virtual size_t read(BeckerSocket *port, uint8_t *buffer, size_t size) = 0;
    virtual ssize_t write(BeckerSocket *port, const uint8_t *buffer, size_t size) = 0;
    virtual ~BeckerState() {}
};

class BeckerStopped : public BeckerState
{
public:
    virtual bool poll(BeckerSocket *port, int ms) override;
    virtual size_t read(BeckerSocket *port, uint8_t *buffer, size_t size) override;
    virtual ssize_t write(BeckerSocket *port, const uint8_t *buffer, size_t size) override;
    static BeckerStopped& getInstance() { static BeckerStopped instance; return instance; }

private:
    BeckerStopped() {}
    BeckerStopped(const BeckerStopped& other);
    BeckerStopped& operator=(const BeckerStopped& other);
};

class BeckerWaitConn : public BeckerState
{
public:
    virtual bool poll(BeckerSocket *port, int ms) override;
    virtual size_t read(BeckerSocket *port, uint8_t *buffer, size_t size) override;
    virtual ssize_t write(BeckerSocket *port, const uint8_t *buffer, size_t size) override;
    static BeckerWaitConn& getInstance() { static BeckerWaitConn instance; return instance; }

private:
    BeckerWaitConn() {}
    BeckerWaitConn(const BeckerWaitConn& other);
    BeckerWaitConn& operator=(const BeckerWaitConn& other);
};

class BeckerConnected : public BeckerState
{
public:
    virtual bool poll(BeckerSocket *port, int ms) override;
    virtual size_t read(BeckerSocket *port, uint8_t *buffer, size_t size) override;
    virtual ssize_t write(BeckerSocket *port, const uint8_t *buffer, size_t size) override;
    static BeckerConnected& getInstance() { static BeckerConnected instance; return instance; }
private:
    BeckerConnected() {}
    BeckerConnected(const BeckerConnected& other);
    BeckerConnected& operator=(const BeckerConnected& other);
};

class BeckerSuspended : public BeckerState
{
public:
    virtual bool poll(BeckerSocket *port, int ms) override;
    virtual size_t read(BeckerSocket *port, uint8_t *buffer, size_t size) override;
    virtual ssize_t write(BeckerSocket *port, const uint8_t *buffer, size_t size) override;
    static BeckerSuspended& getInstance() { static BeckerSuspended instance; return instance; }
private:
    BeckerSuspended() {}
    BeckerSuspended(const BeckerSuspended& other);
    BeckerSuspended& operator=(const BeckerSuspended& other);
};
#else /* !UNUSED */
enum BeckerState {
    BeckerStopped,
    BeckerWaitConn,
    BeckerConnected,
    BeckerSuspended,
};    
#endif /* UNUSED */

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

    size_t do_read(uint8_t *buffer, size_t size);
    ssize_t do_write(const uint8_t *buffer, size_t size);

    ssize_t read_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=BECKER_IOWAIT_MS);
    ssize_t write_sock(const uint8_t *buffer, size_t size, uint32_t timeout_ms=BECKER_IOWAIT_MS);

    bool wait_sock_readable(uint32_t timeout_ms, bool listener=false);
    bool wait_sock_writable(uint32_t timeout_ms);

    void update_fifo() override;
    size_t si_recv(void *buffer, size_t size) override {
        return do_read((uint8_t *) buffer, size); }
    size_t si_send(const void *buffer, size_t size) override {
        return do_write((uint8_t *) buffer, size); }
    
public:
    BeckerSocket();
    virtual ~BeckerSocket();
    void begin(int baud);
    void end() override;

    // mimic UARTManager, baudrate is not used by BeckerSocket
    void setBaudrate(uint32_t baud) override { _baud = baud; }
    uint32_t getBaudrate() override { return _baud; }

    size_t available() override;
    void discardInput() override;
    void flush() override;

#ifdef UNUSED
    // keep BeckerSocket alive
    bool poll(int ms) { return _state->poll(this, ms); }
    // read bytes into buffer
#endif /* UNUSED */

    // specific to BeckerSocket
    void set_host(const char *host, int port);
    const char* get_host(int &port);

    inline BeckerState getState() const { return _state; }
    void setState(BeckerState state) { _state = state; }

#ifdef NOT_SUBCLASS
    // friends, state handlers
    friend class BeckerStopped;
    friend class BeckerWaitConn;
    friend class BeckerConnected;
    friend class BeckerSuspended;
#endif /* NOT_SUBCLASS */
};



#endif // BECKERSOCKET_H
