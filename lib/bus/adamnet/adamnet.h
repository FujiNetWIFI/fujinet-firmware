#ifndef ADAMNET_H
#define ADAMNET_H

/**
 * AdamNet Routines
 */

#include "cmdFrame.h"
#include "UARTChannel.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <map>

enum adamnet_message : uint16_t
{
    ADAMNETMSG_DISKSWAP  // Rotate disk
};

struct adamnet_message_t
{
    adamnet_message message_id;
    uint16_t message_arg;
};

#define ADAMNET_BAUDRATE 62500

// Abort a stalled multi-byte receive after this long with no byte arriving
// (~2.5 byte times). 
#define ADAMNET_RECV_TIMEOUT_US 400

// Minimum turnaround before driving the shared wire in response to a command
#define ADAMNET_TURNAROUND_US 150

// A response must reach the master within this long of the command
#define ADAMNET_RESPONSE_DEADLINE_US 300

// Pace the block-read handshake responses toward a real drive's measured
// turnaround (command-end -> response-start).
#define ADAMNET_DISK_RECV_TURNAROUND_US 256  // CONTROL.RECEIVE -> RESPONSE.ACK
#define ADAMNET_DISK_SEND_TURNAROUND_US 200  // CONTROL.CLR     -> RESPONSE.SEND

// Seek emulation
#define ADAMNET_DISK_SEEK_US 22000
#define ADAMNET_DISK_SEEK_NEWOP_US 130000

// Largest response whose half-duplex echo still fits the RX ring
#define ECHO_DRAIN_MAX 64

// How long to wait for a straggler echo byte to land before giving up. 
#define ECHO_SETTLE_US 50

// A handler that blocked the bus task longer than this leaves a backlog of the
// master's CONTROL.RECEIVE retries piled up in RX (it retries every ~2ms once it
// has ACKed our command and is waiting on the response). 
#define ADAMNET_LONG_CMD_US 10000

// The bus service runs in its own high-priority task
#define ADAMNET_BUS_TASK_PRIORITY 19
#define ADAMNET_BUS_TASK_CORE 1
#define ADAMNET_BUS_TASK_STACK 8192

#define ADAMNET_STALL_RESYNC_US 600

#define MN_RESET 0x00   // command.control (reset)
#define MN_STATUS 0x01  // command.control (status)
#define MN_ACK 0x02     // command.control (ack)
#define MN_CLR 0x03     // command.control (clr) (aka CTS)
#define MN_RECEIVE 0x04 // command.control (receive)
#define MN_CANCEL 0x05  // command.control (cancel)
#define MN_SEND 0x06    // command.control (send)
#define MN_NACK 0x07    // command.control (nack)
#define MN_READY 0x0D   // command.control (ready)

#define NM_STATUS 0x08 // response.control (status)
#define NM_ACK 0x09    // response.control (ack)
#define NM_CANCEL 0x0A // response.control (cancel)
#define NM_SEND 0x0B   // response.data (send)
#define NM_NACK 0x0C   // response.control (nack)

#define ADAMNET_RESET_DEBOUNCE_PERIOD 100 // in ms

#define ADAMNET_DEVTYPE_BLOCK 0x01
#define ADAMNET_DEVTYPE_CHAR 0x00

struct AdamNetPacket
{
    uint8_t cmd_dev;
    uint16_t length;
    uint8_t devtype;
    uint8_t status;
    uint8_t checksum;
} __attribute__((packed));
static_assert(sizeof(AdamNetPacket) == 6, "AdamNetPacket must be 6 bytes");

class systemBus;
class adamFuji;     // declare here so can reference it, but define in fuji.h
class adamPrinter;
class fujiDevice;

/**
 * @brief Calculate checksum for AdamNet packets. Uses a simple 8-bit XOR of each successive byte.
 * @param buf pointer to buffer
 * @param len length of buffer
 * @return checksum value (0x00 - 0xFF)
 */
uint8_t adamnet_checksum(uint8_t *buf, unsigned short len);

/**
 * @brief An AdamNet Device
 */
class virtualDevice
{
    friend systemBus; // We exist on the AdamNet Bus, and need to let it muck with our internals
    friend fujiDevice;

protected:
    /**
     * @brief Send Byte to AdamNet
     * @param b Byte to send via AdamNet
     * @return was byte sent?
     */
    void adamnet_send(uint8_t b);

    /**
     * @brief Send buffer to AdamNet
     * @param buf Buffer to send to AdamNet
     * @param len Length of buffer
     * @return number of bytes sent.
     */
    void adamnet_send_buffer(uint8_t *buf, unsigned short len);

    /**
     * @brief Receive byte from AdamNet
     * @return byte received
     */
    uint8_t adamnet_recv();

    /**
     * @brief convenience function to recieve length
     * @return short containing length.
     */
    uint16_t adamnet_recv_length();

    /**
     * @brief convenience function to receive block number
     * @return ulong containing block num.
     */
    uint32_t adamnet_recv_blockno();

    /**
     * @brief covenience function to send length
     * @param l Length.
     */
    void adamnet_send_length(uint16_t l);

    /**
     * @brief Receive desired # of bytes into buffer from AdamNet
     * @param buf Buffer in which to receive
     * @param len length of buffer
     * @return # of bytes received.
     */
    unsigned short adamnet_recv_buffer(uint8_t *buf, unsigned short len);

    /**
     * @brief Perform reset of device
     */
    virtual void reset();

    /**
     * @brief acknowledge, but not if cmd took too long.
     * @param doNotWaitForIdle do not wait for idle if true.
     */
    virtual void adamnet_response_ack(bool doNotWaitForIdle=false);

    /**
     * @brief non-acknowledge, but not if cmd took too long
     * param doNotWaitForIdle do not wait for idle if true.
     */
    virtual void adamnet_response_nack(bool doNotWaitForIdle=false);

    /**
     * @brief acknowledge if device is ready, but not if cmd took too long.
     */
    virtual void adamnet_control_ready();

    /**
     * @brief Device Number: 0-15
     */
    uint8_t _devnum;

    virtual void shutdown() {}

    /**
     * @brief process the next packet with the active device.
     * @param b first byte of packet.
     */
    virtual void adamnet_process(uint8_t b);

    /**
     * @brief Do any tasks that can only be done when the bus is quiet
     */
    virtual void adamnet_idle();

    /**
     * @brief send current status of device
     */
    virtual void adamnet_control_status();

    /**
     * @brief adam says clear to send!
     */
    virtual void adamnet_control_clr();

    /**
     * @brief send status response
     */
    virtual void adamnet_response_status();

    /**
     * @brief command frame, used by network protocol, ultimately
     */
    cmdFrame_t cmdFrame;

    /**
     * The response sent in adamnet_response_status()
     */
    AdamNetPacket status_response;

    /**
     * Response buffer
     */
    uint8_t response[1024];

    /**
     * Response length
     */
    uint16_t response_len;

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


};

/**
 * @brief The AdamNet Bus
 */
class systemBus
{
private:
    std::map<uint8_t, virtualDevice *> _daisyChain;
    virtualDevice *_activeDev = nullptr;
    adamFuji *_fujiDev = nullptr;
    adamPrinter *_printerDev = nullptr;

    UARTChannel _port;

    // Bytes transmitted while handling the current command; lets us drain
    // exactly our own half-duplex bus echo afterward.
    size_t _tx_count = 0;

    void _adamnet_process_cmd();
    void _adamnet_process_queue();

public:
    void setup();
    void service();
    // Start the dedicated high-priority core-1 bus service task. Call once, after
    // all devices are registered and disks mounted (BUILD_ADAM only).
    void start_bus_task();
    void shutdown();
    void reset();

    /**
     * @brief Wait for AdamNet bus to become idle.
     */
    void wait_for_idle();

    /**
     * @brief Hold off driving the shared one-wire bus until at least
     *        ADAMNET_TURNAROUND_US after the current command, so the response
     *        doesn't collide with the master still releasing the line.
     */
    void min_turnaround();

    /**
     * @brief Hold off driving the wire until at least @p us microseconds after
     *        the current command (measured from start_time). Lets a device pace
     *        its response to match real hardware's turnaround.
     */
    void wait_turnaround(uint32_t us);

    /**
     * @brief Consume the half-duplex echo of a response we just transmitted.
     *        @p n is the number of bytes sent; exactly that many are read back
     *        and discarded so a following master command is left intact. A
     *        response too large for the RX ring is drained by idle detection.
     */
    void drain_echo(size_t n);

    /**
     * stopwatch
     */
    int64_t start_time;

    /**
     * @brief Set true when a multi-byte receive times out mid-packet, so a
     *        handler can abort the current transaction instead of acting on a
     *        truncated payload. Cleared at the start of each command.
     */
    bool frame_error = false;

    /**
     * @brief Set true by a handler that intentionally produced NO response and
     *        wants the master to re-poll (the disk seek stall). The bus task then
     *        only yields and returns: nothing was transmitted so there is no echo
     *        to drain, and it must NOT discardInput() (that 180us FIFO-clear eats
     *        the master's re-poll). Cleared at the start of each command.
     */
    bool stall_silent = false;

    int numDevices();
    void addDevice(virtualDevice *pDevice, uint8_t device_id);
    void remDevice(virtualDevice *pDevice);
    void remDevice(uint8_t device_id);
    bool deviceExists(uint8_t device_id);
    void enableDevice(uint8_t device_id);
    void disableDevice(uint8_t device_id);
    virtualDevice *deviceById(uint8_t device_id);
    void changeDeviceId(virtualDevice *pDevice, uint8_t device_id);
    bool deviceEnabled(uint8_t device_id);
    QueueHandle_t qAdamNetMessages = nullptr;

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    // Everybody thinks "oh I know how a serial port works, I'll just
    // access it directly and bypass the bus!" ಠ_ಠ
    size_t read(void *buffer, size_t length) { return _port.read(buffer, length); }
    size_t read() { return _port.read(); }
    size_t write(const void *buffer, size_t length) { _tx_count += length; return _port.write(buffer, length); }
    size_t write(int n) { _tx_count += 1; return _port.write(n); }
    size_t available() { return _port.available(); }
    void flush() { _port.flushOutput(); }

    // Protect a large response (a 1028-byte disk block) while it streams.
    void quiet_rx_for_send(bool on) {
#ifdef ESP_PLATFORM
        _port.setRXThreshold(on ? 120 : 1);
#endif
    }
    size_t print(int n, int base = 10) { return _port.print(n, base); }
    size_t print(const char *str) { return _port.print(str); }
    size_t print(const std::string &str) { return _port.print(str); }
};

extern systemBus SYSTEM_BUS;

#endif /* ADAMNET_H */
