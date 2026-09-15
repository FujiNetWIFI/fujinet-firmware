#ifndef ADAMNET_H
#define ADAMNET_H

/**
 * AdamNet Routines
 */

#include "bus.h"
#include "AdamNetPhase.h"
#include "FujiAdamPacket.h"
#include "UARTChannel.h"
#include "BoIPChannel.h"
#include "global_types.h"

#include <map>

#define FUJI_COMMAND_PACKET FujiAdamPacket

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

// A handler that blocked the bus task longer than this leaves a backlog of the
// master's CONTROL.RECEIVE retries piled up in RX (it retries every ~2ms once it
// has ACKed our command and is waiting on the response).
#define ADAMNET_LONG_CMD_US 10000

// The bus service runs in its own high-priority task
#define ADAMNET_BUS_TASK_PRIORITY 19
#define ADAMNET_BUS_TASK_CORE 1
#define ADAMNET_BUS_TASK_STACK 8192

#define ADAMNET_STALL_RESYNC_US 600

#define ADAMNET_RESET_DEBOUNCE_PERIOD 100 // in ms

enum class ADAMNET_DEVTYPE : uint8_t {
    CHAR = 0x00,
    BLOCK = 0x01,
};

struct AdamNetStatus
{
    u16le_t length;
    ADAMNET_DEVTYPE devtype;
    uint8_t status;
} __attribute__((packed));
static_assert(sizeof(AdamNetStatus) == 4, "AdamNetStatus must be 4 bytes");

class systemBus;
class adamFuji;     // declare here so can reference it, but define in fuji.h
class adamPrinter;
class fujiDevice;

/**
 * @brief An AdamNet Device
 */
class virtualDevice
{
    friend systemBus; // We exist on the AdamNet Bus, and need to let it muck with our internals
    friend fujiDevice;

protected:
    /**
     * @brief Perform reset of device
     */
    virtual void reset();

    virtual void adamnet_control_ready();   // Check if device is ready for command
    virtual void adamnet_control_receive(); // Tell device to queue up data
    virtual void adamnet_control_send(const FujiAdamPacket &packet) = 0; // Send data to device

    virtual AdamNetStatus deviceStatus() = 0;

    // PC/BoIP: bypass the 300us response window (a slow host can blow it). Only
    // for re-polled block devices; single-shot devices keep it.
    bool _pc_no_response_deadline = false;

    virtual void shutdown() {}

    /**
     * @brief Do any tasks that can only be done when the bus is quiet
     */
    virtual void adamnet_idle();

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
    fujiDeviceID_t id();
};

/**
 * @brief The AdamNet Bus
 */
class systemBus : public SystemBusBase
{
private:
    virtualDevice *_activeDev = nullptr;
    const FujiAdamPacket *_activePacket;

    // _port = UART on hardware, or a TCP socket to an emulator on PC (Bus over IP).
    UARTChannel _serial;
#ifndef ESP_PLATFORM
    BoIPChannel _netadam;
#endif
    IOChannel *_port = nullptr;

    void _adamnet_process_cmd();
#ifdef ESP_PLATFORM
    void _adamnet_process_queue();
#endif /* ESP_PLATFORM */
    void _adamnet_dispatch(const FujiAdamPacket &packet);

    std::optional<ByteBuffer> _transaction_reply_encoded;
    bool _stall_silent = false;
    AdamNetPhase busPhase;

    /**
     * @brief Wait for AdamNet bus to become idle.
     */
    void wait_for_idle();

    bool sendReplyPacket(bool ack, const void *data, size_t length);
    inline bool sendReplyPacket(bool ack, const ByteBuffer &data) {
        return sendReplyPacket(ack, data.data(), data.size());
    }
    inline bool sendReplyPacket(bool ack) {
        return sendReplyPacket(ack, NULL, 0);
    }
    void sendStatusPacket(const AdamNetStatus &status);
    void sendResponsePacket(void);
    bool writeBusPacket(const FujiAdamPacket &packet);

public:
    void setup();
    void service();
    // Start the dedicated high-priority core-1 bus service task. Call once, after
    // all devices are registered and disks mounted (BUILD_ADAM only).
    void start_bus_task();
    void shutdown();
    void reset();

    /**
     * stopwatch
     */
    int64_t _start_time;

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
    void stall_silent() {
        _stall_silent = false;
        busPhase.didIgnore();
    }

    bool deviceExists(fujiDeviceID_t device_id) {
        return _daisyChain.deviceWithFujiID(device_id) != nullptr;
    }
    bool deviceEnabled(fujiDeviceID_t device_id) {
        auto device = _daisyChain.deviceWithFujiID(device_id);
        if (device)
            return device->device_active;
        return false;
    }

#ifdef ESP_PLATFORM
    QueueHandle_t qAdamNetMessages = nullptr;
#endif /* ESP_PLATFORM */

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    void transaction_accept(transState_t expectMoreData) override;
    void transaction_success() override;
    void transaction_error() override;
    using SystemBusBase::transaction_get;
    success_is_true transaction_get(void *data, size_t len) override;
    using SystemBusBase::transaction_send;
    void transaction_send(const void *data, size_t len, bool is_error=false) override;

    void sendAckPacket();
    void sendNakPacket();

#ifdef ESP_PLATFORM
    void adamnet_handle_bus_task();
#endif /* ESP_PLATFORM */

};

extern systemBus SYSTEM_BUS;

#endif /* ADAMNET_H */
