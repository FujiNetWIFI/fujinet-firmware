#ifndef rc2014_H
#define rc2014_H

/**
 * rc2014 Routines
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <array>

#include <map>

#define RC2014SIO_BAUDRATE 115200

#define rc2014_RESET_DEBOUNCE_PERIOD 100 // in ms

constexpr int RC2014_TX_BUFFER_SIZE = 1024;
constexpr int RC2014_RX_BUFFER_SIZE = 1024;

union cmdFrame_t
{
    struct
    {
        uint8_t device;
        uint8_t comnd;
        uint8_t aux1;
        uint8_t aux2;
        uint8_t cksum;
    };
    struct
    {
        uint32_t commanddata;
        uint8_t checksum;
    } __attribute__((packed));
};


// buffer with inbuilt checksum
// -- instantiate with desired buffer size + 1 for checksum
template <size_t BUFFER_SIZE>
class rc2014Fifo {
public:
    rc2014Fifo() { clear(); };


    bool is_empty() {
        std::scoped_lock<std::mutex> lock(_m);
        return (_i_head == _i_tail);
    };

    bool is_full() {
        std::scoped_lock<std::mutex> lock(_m);
        return ((_i_head + 1) % BUFFER_SIZE) == _i_tail;
    };

    bool is_overrun() {
        std::scoped_lock<std::mutex> lock(_m);
        return _overrun;
    };

    bool is_underrun() {
        std::scoped_lock<std::mutex> lock(_m);
        return _underrun;
    };

    void push(uint8_t val) {
        if (is_full()) {
            _overrun = true;
            return;
        }

        std::scoped_lock<std::mutex> lock(_m);
        _fifo[_i_head] = val;
        _i_head = (_i_head + 1) % BUFFER_SIZE;
    };

    void push(uint8_t* buffer, size_t len) {
        for (size_t i = 0; i < len; i++)
            push(buffer[i]);
    }

    void push(const char* str) {
        push(std::string(str));
    }

    void push(const std::string& str) {
        for (auto &ch : str)
            push(ch);
    }

    uint8_t pop() {
        if (is_empty()) {
            _overrun = true;
            return 0xff;
        }

        std::scoped_lock<std::mutex> lock(_m);
        uint8_t val = _fifo[_i_tail];
        _i_tail = (_i_tail + 1) % BUFFER_SIZE;
        return val;
    };

    void pop(uint8_t* buffer, size_t len) {
        for (size_t i = 0; i < len; i++)
            buffer[i] = pop();
    }

    void clear() {
        std::scoped_lock<std::mutex> lock(_m);
        _i_tail = _i_head = 0;
        _overrun = _underrun = false;
    };

    size_t avail() {
        std::scoped_lock<std::mutex> lock(_m);
        if (_i_head == _i_tail)
            return 0; // Empty buffer

        return (_i_head - _i_tail + BUFFER_SIZE) % BUFFER_SIZE;
    }

private:
    std::mutex _m;
    std::array<uint8_t, BUFFER_SIZE> _fifo;
    unsigned _i_tail;
    unsigned _i_head;
    bool _overrun;
    bool _underrun;
};

class systemBus;
class rc2014Fuji;     // declare here so can reference it, but define in fuji.h
class rc2014CPM;
class rc2014Modem;
class rc2014Printer;

/**
 * @brief Calculate checksum for rc2014 packets. Uses a simple 8-bit XOR of each successive byte.
 * @param buf pointer to buffer
 * @param len length of buffer
 * @return checksum value (0x00 - 0xFF)
 */
uint8_t rc2014_checksum(uint8_t *buf, unsigned short len);

/**
 * @brief An rc2014 Device
 */
class virtualDevice
{
protected:
    friend systemBus; // We exist on the rc2014 Bus, and need its methods.

    /**
     * @brief Send Byte to rc2014
     * @param b Byte to send via rc2014
     * @return none
     */
    void rc2014_send(uint8_t b);

    /**
     * @brief Send string buffer to rc2014
     * @param s String to send to rc2014
     * @return none
     */
    void rc2014_send_string(const std::string& s);

    /**
     * @brief Send integer as string to rc2014
     * @param s String to send to rc2014
     * @return none
     */
    void rc2014_send_int(int i);

    /**
     * @brief Flush output to rc2014
    */
    void rc2014_flush();

    /**
     * @brief Send buffer to rc2014
     * @param buf Buffer to send to rc2014
     * @param len Length of buffer
     * @return number of bytes sent.
     */
    size_t rc2014_send_buffer(const uint8_t *buf, unsigned short len);

    /**
     * @brief transmit buffer availability before a flush is needed
     * @return number of bytes available.
    */
    size_t rc2014_send_available();

    /**
     * @brief Receive byte from rc2014
     * @return byte received
     */
    uint8_t rc2014_recv();

    /**
     * How many bytes available?
    */
    int rc2014_recv_available();

    /**
     * @brief Receive byte from rc2014 with a timeout period
     * @param dur timeout period in milliseconds
     * @return true = timeout, false = b contains byte received
     */
    bool rc2014_recv_timeout(uint8_t *b, uint64_t dur);

    /**
     * @brief convenience function to recieve length
     * @return short containing length.
     */
    uint16_t rc2014_recv_length();

    /**
     * @brief convenience function to receive block number
     * @return ulong containing block num.
     */
    uint32_t rc2014_recv_blockno();

    /**
     * @brief covenience function to send length
     * @param l Length.
     */
    void rc2014_send_length(uint16_t l);

    /**
     * @brief Receive desired # of bytes into buffer from rc2014
     * @param buf Buffer in which to receive
     * @param len length of buffer
     * @return # of bytes received.
     */
    unsigned short rc2014_recv_buffer(uint8_t *buf, unsigned short len);

    void rc2014_stream_send(uint8_t b);
    uint8_t rc2014_stream_recv();

    /**
     * @brief Perform reset of device
     */
    virtual void reset();

    /**
     * @brief acknowledge, but not if cmd took too long.
     */
    virtual void rc2014_send_ack();

    /**
     * @brief non-acknowledge, but not if cmd took too long
     */
    virtual void rc2014_send_nak();

    /**
     * @brief send complete signal, but not if cmd took too long
     */
    virtual void rc2014_send_complete();

    /**
     * @brief send complete signal, but not if cmd took too long
     */
    virtual void rc2014_send_error();

    /**
     * @brief acknowledge if device is ready, but not if cmd took too long.
     */
    virtual void rc2014_control_ready();

    /**
     * @brief Device Number: 0-255
     */
    uint8_t _devnum;

    virtual void shutdown() {}

    /**
     * @brief All RS232 devices repeatedly call this routine to fan out to other methods for each command.
     * This is typcially implemented as a switch() statement.
     */
    virtual void rc2014_process(uint32_t commanddata, uint8_t checksum) = 0;

    /**
     * @brief Do any tasks that can only be done when the bus is quiet
     */
    virtual void rc2014_idle();

    /**
     * @brief send current status of device
     */
    virtual void rc2014_control_status();

    /**
     * @brief send status response
     */
    virtual void rc2014_response_status();

    /**
     * @brief handle the uart stream when not used for command
    */
    virtual void rc2014_handle_stream();

    /**
     * @brief poll device to see if an interrupt needs to be raised
    */
    virtual bool rc2014_poll_interrupt();


    /**
     * @brief command frame, used by network protocol, ultimately
     */
    cmdFrame_t cmdFrame;

    /**
     * The response sent in rc2014_response_status()
     */
    uint8_t status_response[6] = {0x80,0x00,0x00,0x01,0x00,0x00};

public:

    /**
     * @brief Is this virtualDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

    /**
     * @brief return the device number (0-15) of this device
     * @return the device # (0-15) of this device
     */
    uint8_t id() { return _devnum; }

protected:
    rc2014Fifo<1024> streamFifoTx; // streamed data from rc2014
    rc2014Fifo<1024> streamFifoRx; // streamed data destined for rc2014
};

/**
 * @brief The rc2014 Bus
 */
class systemBus
{
private:
    std::map<uint8_t, virtualDevice *> _daisyChain;
    virtualDevice *_streamDev = nullptr;

    void _rc2014_process_cmd();
    void _rc2014_process_data();
    void _rc2014_process_queue();
    bool _rc2014_poll_interrupts();

    std::array<uint8_t, RC2014_RX_BUFFER_SIZE> _rx_buffer;
    std::array<uint8_t, RC2014_TX_BUFFER_SIZE> _tx_buffer;
    unsigned int _tx_buffer_index = 0;

public:
    void setup();
    void service();
    void shutdown();
    void reset();

    /**
     * @brief Wait for rc2014 bus to become idle.
     */
    void wait_for_idle();

    /**
     * stopwatch
     */
    int64_t start_time;

    int numDevices();
    void addDevice(virtualDevice *pDevice, uint8_t device_id);
    void remDevice(virtualDevice *pDevice);
    void remDevice(uint8_t device_id);
    bool deviceExists(uint8_t device_id);
    void enableDevice(uint8_t device_id);
    void disableDevice(uint8_t device_id);
    bool enabledDeviceStatus(uint8_t device_id);
    void streamDevice(uint8_t device_id);
    void streamDeactivate();
    virtualDevice *deviceById(uint8_t device_id);
    void changeDeviceId(virtualDevice *pDevice, uint8_t device_id);
    QueueHandle_t qrc2014Messages = nullptr;

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    /**
     * @brief Send buffer to rc2014 via SPI
     * @param buf Buffer to send to rc2014
     * @param len Length of buffer
     * @return number of bytes sent.
     */
    size_t busTxBuffer(const uint8_t *buf, unsigned short len);
    size_t busTxByte(const uint8_t byte);
    size_t busTxAvail();
    size_t busTxTransfer();

    size_t busRxBuffer(uint8_t *buf, unsigned short len);
};

extern systemBus SYSTEM_BUS;

#endif /* rc2014_H */
