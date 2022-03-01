#ifndef s100spi_H
#define s100spi_H

/**
 * s100spi Routines
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <map>

#define s100spi_DEVICE_ID_KEYBOARD 0x01
#define s100spi_DEVICE_ID_PRINTER  0x02
#define s100spi_DEVICEID_DISK      0x04
#define s100spi_DEVICE_TAPE        0x08
#define s100spi_DEVICE_NETWORK     0x0E
#define s100spi_DEVICE_FUJINET     0x0F

#define s100spi_RESET_DEBOUNCE_PERIOD 100 // in ms

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

class systemBus;
class s100spiFuji;     // declare here so can reference it, but define in fuji.h
class s100spiPrinter;

/**
 * @brief Calculate checksum for s100spi packets. Uses a simple 8-bit XOR of each successive byte.
 * @param buf pointer to buffer
 * @param len length of buffer
 * @return checksum value (0x00 - 0xFF)
 */
uint8_t s100spi_checksum(uint8_t *buf, unsigned short len);

/**
 * @brief An s100spi Device
 */
class virtualDevice
{
protected:
    friend systemBus; // We exist on the s100spi Bus, and need its methods.

    /**
     * @brief Send Byte to s100spi
     * @param b Byte to send via s100spi
     * @return was byte sent?
     */
    void s100spi_send(uint8_t b);

    /**
     * @brief Send buffer to s100spi
     * @param buf Buffer to send to s100spi
     * @param len Length of buffer
     * @return number of bytes sent.
     */
    void s100spi_send_buffer(uint8_t *buf, unsigned short len);

    /**
     * @brief Receive byte from s100spi
     * @return byte received
     */
    uint8_t s100spi_recv();

    /**
     * @brief Receive byte from s100spi with a timeout period
     * @param dur timeout period in milliseconds
     * @return true = timeout, false = b contains byte received
     */
    bool s100spi_recv_timeout(uint8_t *b, uint64_t dur);

    /**
     * @brief convenience function to recieve length
     * @return short containing length.
     */
    uint16_t s100spi_recv_length();

    /**
     * @brief convenience function to receive block number
     * @return ulong containing block num.
     */
    uint32_t s100spi_recv_blockno();

    /**
     * @brief covenience function to send length
     * @param l Length.
     */
    void s100spi_send_length(uint16_t l);

    /**
     * @brief Receive desired # of bytes into buffer from s100spi
     * @param buf Buffer in which to receive
     * @param len length of buffer
     * @return # of bytes received.
     */
    unsigned short s100spi_recv_buffer(uint8_t *buf, unsigned short len);

    /**
     * @brief Perform reset of device
     */
    virtual void reset();

    /**
     * @brief acknowledge, but not if cmd took too long.
     */
    virtual void s100spi_response_ack();

    /**
     * @brief non-acknowledge, but not if cmd took too long
     */
    virtual void s100spi_response_nack();

    /**
     * @brief acknowledge if device is ready, but not if cmd took too long.
     */
    virtual void s100spi_control_ready();

    /**
     * @brief Device Number: 0-15
     */
    uint8_t _devnum;

    virtual void shutdown() {}

    /**
     * @brief process the next packet with the active device.
     * @param b first byte of packet.
     */
    virtual void s100spi_process(uint8_t b);

    /**
     * @brief Do any tasks that can only be done when the bus is quiet
     */
    virtual void s100spi_idle();
    
    /**
     * @brief send current status of device
     */
    virtual void s100spi_control_status();

    /**
     * @brief send status response
     */
    virtual void s100spi_response_status();
    
    /**
     * @brief command frame, used by network protocol, ultimately
     */
    cmdFrame_t cmdFrame;

    /**
     * The response sent in s100spi_response_status()
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

    
};

/**
 * @brief The s100spi Bus
 */
class systemBus
{
private:
    std::map<uint8_t, virtualDevice *> _daisyChain;
    virtualDevice *_activeDev = nullptr;
    s100spiFuji *_fujiDev = nullptr;
    s100spiPrinter *_printerDev = nullptr;

    void _s100spi_process_cmd();
    void _s100spi_process_queue();

public:
    void setup();
    void service();
    void shutdown();
    void reset();

    /**
     * @brief Wait for s100spi bus to become idle.
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
    virtualDevice *deviceById(uint8_t device_id);
    void changeDeviceId(virtualDevice *pDevice, uint8_t device_id);
    QueueHandle_t qs100spiMessages = nullptr;
};

extern systemBus s100Bus;

#endif /* s100spi_H */