#ifndef CX16_I2C_H
#define CX16_I2C_H

#include <cstdint>
#include <forward_list>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <utility>
#include <string>

#define CX16_DEVICEID_DISK 0x31
#define CX16_DEVICEID_DISK_LAST 0x3F

#define CX16_DEVICEID_PRINTER 0x40
#define CX16_DEVICEID_PRINTER_LAST 0x43

#define CX16_DEVICEID_FN_VOICE 0x43

#define CX16_DEVICEID_APETIME 0x45

#define CX16_DEVICEID_TYPE3POLL 0x4F

#define CX16_DEVICEID_RS232 0x50
#define CX16_DEVICEID_RS2323_LAST 0x53

#define CX16_DEVICEID_CASSETTE 0x5F

#define CX16_DEVICEID_FUJINET 0x70
#define CX16_DEVICEID_FN_NETWORK 0x71
#define CX16_DEVICEID_FN_NETWORK_LAST 0x78

#define CX16_DEVICEID_MIDI 0x99

#define CX16_DEVICEID_SIO2BT_NET 0x4E
#define CX16_DEVICEID_SIO2BT_SMART 0x45 // Doubles as APETime and "High Score Submission" to URL
#define CX16_DEVICEID_APE 0x45
#define CX16_DEVICEID_ASPEQT 0x46
#define CX16_DEVICEID_PCLINK 0x6F

#define CX16_DEVICEID_CPM 0x5A

#define I2C_SLAVE_TX_BUF_LEN 255 
#define I2C_SLAVE_RX_BUF_LEN 32
#define I2C_DEVICE_ID 0x70

/**
 * | Address | R/W | Description
 * |---      |---  |---
 * | 0       | W   | Device ID
 * | 1       | W   | Command 
 * | 2       | W   | Aux1 
 * | 3       | W   | Aux2 
 * | 4       | W   | Checksum of addresses 0-3
 * | 5       | R   | (A)CK/(N)ACK
 * | 6       | R   | (C)OMPLETE/(E)RROR
 * | 7       | R/W | Payload Length (LO)
 * | 8       | R/W | Payload Length (HI)
 * | 9       | R/W | Payload Data (auto-increment) 
 * 
 * Any write to address 0 will zero out all other addresses.
 * Read to address 0 to perform command
 * 
 * So the sequence to perform a command to the fujinet:
 * 
 * 1. Write addresses 0-4 for command frame.
 * 2. if a write payload is needed, write length lo/hi, then write payload data for # of bytes in length
 * 3. READ from address 0 to perform command.
 * 4. Check ACK/NAK
 * 5. Check COMPLETE/ERROR
 * 
 * Alternatively, to perform a command from fujinet to the CX16:
 * 
 * 1. Write addresses 0-4 for the command frame.
 * 2. If a read payload is expected, write length lo/hi.
 * 3. READ from address 0 to perform command.
 * 4. Check ACK/NAK
 * 5. Check COMPLETE/ERROR
 * 6. If payload expected, read Payload Data for as many expected bytes.
 */

/**
 * @brief The command frame
 */
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

/**
 * @brief calculate a simple 8-bit wrap-around checksum.
 * @param buf Pointer to buffer
 * @param len Length of buffer
 * @return the 8-bit checksum value
 */
uint8_t cx16_checksum(uint8_t *buf, unsigned short len);

/**
 * @class Forward declaration of System Bus
 */
class systemBus;

/**
 * @class virtualDevice
 * @brief All #FujiNet devices derive from this.
 */
class virtualDevice
{
protected:
    friend systemBus; /* Because we connect to it. */

    /**
     * @brief The device number (ID)
     */
    int _devnum;

    /**
     * @brief The passed in command frame, copied.
     */
    cmdFrame_t cmdFrame;     

    /**
     * @brief Message queue
     */
    QueueHandle_t qMessages = nullptr;

    /**
     * @brief Send the desired buffer to the CX16.
     * @param buff The byte buffer to send to the CX16.
     * @param len The length of the buffer to send to the CX16.
     * @return TRUE if the CX16 processed the data in error, FALSE if the Cx16 successfully processed
     * the data.
     */
    void bus_to_computer(uint8_t *buff, uint16_t len, bool err);

    /**
     * @brief Receive data from the CX16.
     * @param buff The byte buffer provided for data from the CX16.
     * @param len The length of the amount of data to receive from the CX16.
     * @return An 8-bit wrap-around checksum calculated by the CX16, which should be checked with cx16_checksum()
     */
    uint8_t bus_to_peripheral(uint8_t *buff, uint16_t len);

    /**
     * @brief Send an acknowledgement byte to the Cx16 'A'
     * This should be used if the command received by the CX16 device is valid, and is used to signal to the
     * Cx16 that we are now processing the command.
     */
    void cx16_ack();

    /**
     * @brief Send a non-acknowledgement (NAK) to the Cx16 'N'
     * This should be used if the command received by the CX16 device is invalid, in the first place. It is not
     * the same as cx16_error().
     */
    void cx16_nak();

    /**
     * @brief Send a COMPLETE to the Cx16 'C'
     * This should be used after processing of the command to indicate that we've successfully finished. Failure to send
     * either a COMPLETE or ERROR will result in a CX16 TIMEOUT (138) to be reported in DSTATS.
     */
    void cx16_complete();

    /**
     * @brief Send an ERROR to the Cx16 'E'
     * This should be used during or after processing of the command to indicate that an error resulted
     * from processing the command, and that the Cx16 should probably re-try the command. Failure to
     * send an ERROR or COMPLTE will result in a CX16 TIMEOUT (138) to be reported in DSTATS.
     */
    void cx16_error();

    /**
     * @brief Return the two aux bytes in cmdFrame as a single 16-bit value, commonly used, for example to retrieve
     * a sector number, for disk, or a number of bytes waiting for the cx16Network device.
     *
     * @return 16-bit value of DAUX1/DAUX2 in cmdFrame.
     */
    unsigned short cx16_get_aux() { return cmdFrame.aux1 | (cmdFrame.aux2 << 8); };

    /**
     * @brief All CX16 commands by convention should return a status command, using bus_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void status() = 0;

    /**
     * @brief All CX16 devices repeatedly call this routine to fan out to other methods for each command.
     * This is typcially implemented as a switch() statement.
     */
    virtual void process(uint32_t commanddata, uint8_t checksum) = 0;

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief get the CX16 device Number (1-255)
     * @return The device number registered for this device
     */
    int id() { return _devnum; };

    /**
     * @brief Is this virtualDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

    /**
     * @brief Get the systemBus object that this virtualDevice is attached to.
     */
    systemBus get_bus();
};

/**
 * @class systemBus
 * @brief the system bus that all virtualDevices attach to.
 */
class systemBus
{
private:
    /**
     * @brief The chain of devices on the bus.
     */
    std::forward_list<virtualDevice *> _daisyChain;

    /**
     * @brief Number of devices on bus
     */
    int _num_devices = 0;

    /**
     * @brief the active device being process()'ed
     */
    virtualDevice *_activeDev = nullptr;


    /**
     * @brief is device shutting down?
    */
    bool shuttingDown = false;

    /**
     * @brief I²C slave port
     */
    int i2c_slave_port = 0;

    /**
     * @brief I²C receive buffer
     */
    uint8_t i2c_buffer[I2C_SLAVE_RX_BUF_LEN];

    /**
     * @brief I²C receive buffer length
     */
    int i2c_buffer_len=0;

    /**
     * @brief I²C receive buffer offset
     */
    uint8_t i2c_buffer_off=0;

    /**
     * @brief I²C register storage
     */
    uint8_t i2c_register[256];

    /**
     * @brief I²C payload storage
     */
    std::string i2c_payload;

    /**
     * @brief I²C payload auto-increment counter.
     */

    /**
     * @brief called to process the next command
     */
    void process_cmd();

    /**
     * @brief called to process a queue item (such as disk swap)
     */
    void process_queue();

public:
    /**
     * @brief called in main.cpp to set up the bus.
     */
    void setup();

    /**
     * @brief called to handle read from address
     */
    void address_read(uint8_t addr);

    /**
     * @brief called to handle write to address
     */
    void address_write(uint8_t addr, uint8_t val);

    /**
     * @brief called to add to payload
     */
    void payload_add(uint8_t *buf, uint16_t len);

    /**
     * @brief Run one iteration of the bus service loop
     */
    void service();

    /**
     * @brief called from main shutdown to clean up the device.
     */
    void shutdown();

    /**
     * @brief Return number of devices on bus.
     * @return # of devices on bus.
     */
    int numDevices() { return _num_devices; };

    /**
     * @brief Add device to bus.
     * @param pDevice Pointer to virtualDevice
     * @param device_id The ID to assign to virtualDevice
     */
    void addDevice(virtualDevice *pDevice, int device_id);

    /**
     * @brief Remove device from bus
     * @param pDevice pointer to virtualDevice
     */
   void remDevice(virtualDevice *pDevice);

    /**
     * @brief Return pointer to device given ID
     * @param device_id ID of device to return.
     * @return pointer to virtualDevice
     */
    virtualDevice *deviceById(int device_id);

    /**
     * @brief Change ID of a particular virtualDevice
     * @param pDevice pointer to virtualDevice
     * @param device_id new device ID 
     */
    void changeDeviceId(virtualDevice *pDevice, int device_id);

    /**
     * @brief Are we shutting down?
     * @return value of shuttingDown
     */
    bool getShuttingDown() { return shuttingDown; }
};

/**
 * @brief Return
 */
extern systemBus CX16;

#endif /* CX16_I2C_H */