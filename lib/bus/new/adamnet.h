#ifndef ADAMNET_H
#define ADAMNET_H

/**
 * AdamNet Routines
 */

#include <map>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define ADAMNET_BAUD 62500

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

#define ADAMNET_DEVICE_ID_KEYBOARD 0x01
#define ADAMNET_DEVICE_ID_PRINTER  0x02
#define ADAMNET_DEVICEID_DISK      0x04
#define ADAMNET_DEVICE_TAPE        0x08
#define ADAMNET_DEVICE_NETWORK     0x0E
#define ADAMNET_DEVICE_FUJINET     0x0F

#define ADAMNET_RESET_DEBOUNCE_PERIOD 100 // in ms

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

class adamNetBus;
class adamFuji;     // declare here so can reference it, but define in fuji.h
class adamPrinter;

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
class adamNetDevice
{
protected:
    friend adamNetBus; // We exist on the AdamNet Bus, and need its methods.

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
     * @brief Receive byte from AdamNet with a timeout period
     * @param dur timeout period in milliseconds
     * @return true = timeout, false = b contains byte received
     */
    bool adamnet_recv_timeout(uint8_t *b, uint64_t dur);

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
     */
    virtual void adamnet_response_ack();

    /**
     * @brief non-acknowledge, but not if cmd took too long
     */
    virtual void adamnet_response_nack();

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
    uint8_t status_response[6] = {0x80,0x00,0x00,0x01,0x00,0x00};

public:

    /**
     * @brief Is this sioDevice holding the virtual disk drive used to boot CONFIG?
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
class adamNetBus
{
private:
    std::map<uint8_t, adamNetDevice *> _daisyChain;
    adamNetDevice *_activeDev = nullptr;
    adamFuji *_fujiDev = nullptr;
    adamPrinter *_printerDev = nullptr;

    void _adamnet_process_cmd();
    void _adamnet_process_queue();

public:
    void setup();
    void service();
    void shutdown();
    void reset();

    /**
     * @brief Wait for AdamNet bus to become idle.
     */
    void wait_for_idle();

    /**
     * stopwatch
     */
    int64_t start_time;

    int numDevices();
    void addDevice(adamNetDevice *pDevice, uint8_t device_id);
    void remDevice(adamNetDevice *pDevice);
    void remDevice(uint8_t device_id);
    bool deviceExists(uint8_t device_id);
    void enableDevice(uint8_t device_id);
    void disableDevice(uint8_t device_id);
    adamNetDevice *deviceById(uint8_t device_id);
    void changeDeviceId(adamNetDevice *pDevice, uint8_t device_id);
    QueueHandle_t qAdamNetMessages = nullptr;
};

extern adamNetBus AdamNet;

#endif /* ADAMNET_H */