#ifndef COMLYNX_H
#define COMLYNX_H

/**
 * Comlynx Routines
 */

#include "cmdFrame.h"
#include "UARTChannel.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <forward_list>
#include <map>


#define COMLYNX_BAUDRATE 62500

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


#define COMLYNX_RESET_DEBOUNCE_PERIOD 100 // in ms

class systemBus;
class lynxFuji;     // declare here so can reference it, but define in fuji.h
class lynxPrinter;
class lynxUDPStream; // declare here so can reference it, but define in udpstream.h
class lynxNetwork;

/**
 * @brief Calculate checksum for Comlynx packets. Uses a simple 8-bit XOR of each successive byte.
 * @param buf pointer to buffer
 * @param len length of buffer
 * @return checksum value (0x00 - 0xFF)
 */
uint8_t comlynx_checksum(uint8_t *buf, unsigned short len);

/**
 * @brief An Comlynx Device
 */
class virtualDevice
{
protected:
    friend systemBus; // We exist on the Comlynx Bus, and need its methods.

    /**
     * @brief Send Byte to Comlynx
     * @param b Byte to send via Comlynx
     * @return was byte sent?
     */
    void comlynx_send(uint8_t b);

    /**
     * @brief Send buffer to Comlynx
     * @param buf Buffer to send to Comlynx
     * @param len Length of buffer
     * @return number of bytes sent.
     */
    void comlynx_send_buffer(uint8_t *buf, unsigned short len);

    /**
     * @brief Receive checksum byte and compare with checksum calculation
     * @return true or false
     */
    bool comlynx_recv_ck();

    /**
     * @brief Receive byte from Comlynx
     * @return byte received
     */
    uint8_t comlynx_recv();

    /**
     * @brief Receive byte from Comlynx with a timeout period
     * @param dur timeout period in milliseconds
     * @return true = timeout, false = b contains byte received
     */
    bool comlynx_recv_timeout(uint8_t *b, uint64_t dur);

    /**
     * @brief convenience function to recieve length
     * @return short containing length.
     */
    uint16_t comlynx_recv_length();

    /**
     * @brief convenience function to receive block number
     * @return ulong containing block num.
     */
    uint32_t comlynx_recv_blockno();

    /**
     * @brief covenience function to send length
     * @param l Length.
     */
    void comlynx_send_length(uint16_t l);

    /**
     * @brief Receive desired # of bytes into buffer from Comlynx
     * @param buf Buffer in which to receive
     * @param len length of buffer
     * @return # of bytes received.
     */
    unsigned short comlynx_recv_buffer(uint8_t *buf, unsigned short len);

    /**
     * @brief Perform reset of device
     */
    virtual void reset();

    /**
     * @brief acknowledge, but not if cmd took too long.
     */
    virtual void comlynx_response_ack();

    /**
     * @brief non-acknowledge, but not if cmd took too long
     */
    virtual void comlynx_response_nack();

    /**
     * @brief acknowledge if device is ready, but not if cmd took too long.
     */
    //virtual void comlynx_control_ready();

    /**
     * @brief Device Number: 0-15
     */
    fujiDeviceID_t _devnum;

    virtual void shutdown() {}

    /**
     * @brief process the next packet with the active device.
     * @param b first byte of packet.
     */
    virtual void comlynx_process();

    /**
     * @brief Do any tasks that can only be done when the bus is quiet
     */
    //virtual void comlynx_idle();

    /**
     * @brief send current status of device
     */
    //virtual void comlynx_control_status();

    /**
     * @brief lynx says clear to send!
     */
    //virtual void comlynx_control_clr();

    /**
     * @brief send status response
     */
    //virtual void comlynx_response_status();

    /**
     * @brief command frame, used by network protocol, ultimately
     */
    cmdFrame_t cmdFrame;

    /**
     * The response sent in comlynx_response_status()
     */
    //uint8_t status_response[6] = {0x80,0x00,0x00,0x01,0x00,0x00};

    /**
     * Response buffer and length
     */
    uint8_t response[1024];
    uint16_t response_len;

    /**
     * Receive buffer, length and point into recvbuffer
     */
    uint8_t recvbuffer[1024];
    uint16_t recvbuffer_len = 0;
    uint8_t *recvbuf_pos;

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
    fujiDeviceID_t id() { return _devnum; }


};

/**
 * @brief The Comlynx Bus
 */
class systemBus
{
private:
    std::map<uint8_t, virtualDevice *> _daisyChain;
    virtualDevice *_activeDev = nullptr;
    lynxFuji *_fujiDev = nullptr;
    lynxPrinter *_printerDev = nullptr;

    UARTChannel _port;

    void _comlynx_process_cmd();
    void _comlynx_process_queue();

public:
    lynxUDPStream *_udpDev = nullptr;

    void setup();
    void service();
    void shutdown();
    void reset();

    /**
     * @brief Wait to see if Comlynx bus is idle.
     */
    bool wait_for_idle();

    /**
     * stopwatch
     */
    int64_t start_time;

    int numDevices();
    void addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id);
    void remDevice(virtualDevice *pDevice);
    void remDevice(fujiDeviceID_t device_id);
    bool deviceExists(fujiDeviceID_t device_id);
    void enableDevice(fujiDeviceID_t device_id);
    void disableDevice(fujiDeviceID_t device_id);
    virtualDevice *deviceById(fujiDeviceID_t device_id);
    void changeDeviceId(virtualDevice *pDevice, int device_id);
    bool deviceEnabled(fujiDeviceID_t device_id);
    QueueHandle_t qComlynxMessages = nullptr;
    void setUDPHost(const char *newhost, int port);             // Set new host/ip & port for UDP Stream

    void setRedeyeMode(bool enable);
    void setRedeyeGameRemap(uint32_t remap);

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    // Everybody thinks "oh I know how a serial port works, I'll just
    // access it directly and bypass the bus!" ಠ_ಠ
    size_t read(void *buffer, size_t length) { return _port.read(buffer, length); }
    size_t read() { return _port.read(); }
    size_t write(const void *buffer, size_t length) { return _port.write(buffer, length); }
    size_t write(int n) { return _port.write(n); }
    size_t available() { return _port.available(); }
    void flush() { _port.flushOutput(); }
    size_t print(int n, int base = 10) { return _port.print(n, base); }
    size_t print(const char *str) { return _port.print(str); }
    size_t print(const std::string &str) { return _port.print(str); }
};

extern systemBus SYSTEM_BUS;

#endif /* COMLYNX_H */
