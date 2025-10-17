#ifndef rc2014_H
#define rc2014_H

/**
 * rc2014 Routines
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <map>

#define RC2014SIO_BAUDRATE 115200

#define RC2014_DEVICEID_DISK 0x31
#define RC2014_DEVICEID_DISK_LAST 0x3F

#define RC2014_DEVICEID_PRINTER 0x41
#define RC2014_DEVICEID_PRINTER_LAST 0x44


#define RC2014_DEVICEID_FUJINET 0x70
#define RC2014_DEVICEID_FN_NETWORK 0x71
#define RC2014_DEVICEID_FN_NETWORK_LAST 0x78

#define RC2014_DEVICEID_MODEM 0x50

#define RC2014_DEVICEID_CPM 0x5A


#define rc2014_RESET_DEBOUNCE_PERIOD 100 // in ms

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
    virtual void rc2014_response_nack();

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
    void _rc2014_process_queue();

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
};

extern systemBus SYSTEM_BUS;

#endif /* rc2014_H */
