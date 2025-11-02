#ifndef RS232_H
#define RS232_H

#include "UARTChannel.h"

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif /* ESP_PLATFORM */

#include <forward_list>

#define RS232_BAUDRATE 9600
//#define RS232_BAUDRATE 115200

#define RS232_DEVICEID_DISK            0x31
#define RS232_DEVICEID_DISK_LAST       0x3F

#define RS232_DEVICEID_PRINTER         0x40
#define RS232_DEVICEID_PRINTER_LAST    0x43

#define RS232_DEVICEID_FN_VOICE        0x43

#define RS232_DEVICEID_APETIME         0x45

#define RS232_DEVICEID_RS232           0x50
#define RS232_DEVICEID_RS2323_LAST     0x53

#define RS232_DEVICEID_FUJINET         0x70
#define RS232_DEVICEID_FN_NETWORK      0x71
#define RS232_DEVICEID_FN_NETWORK_LAST 0x78

#define RS232_DEVICEID_MIDI            0x99

#define RS232_DEVICEID_CPM             0x5A

#define DELAY_T4 800
#define DELAY_T5 800

#define DIRECTION_NONE    0x00
#define DIRECTION_READ    0x40
#define DIRECTION_WRITE   0x80

typedef struct
{
    uint8_t device;
    uint8_t comnd;
    union {
        struct {
            uint8_t aux1;
            uint8_t aux2;
            uint8_t aux3;
            uint8_t aux4;
        };
        struct {
            uint16_t aux12;
            uint16_t aux34;
        };
        uint32_t aux;
    };
    uint8_t cksum;
} __attribute__((packed)) cmdFrame_t;

// helper functions
uint8_t rs232_checksum(uint8_t *buf, unsigned short len);

// class def'ns
class rs232Modem;    // declare here so can reference it, but define in modem.h
class rs232Fuji;     // declare here so can reference it, but define in fuji.h
class systemBus;      // declare early so can be friend
class rs232Network;  // declare here so can reference it, but define in network.h
class rs232UDPStream; // declare here so can reference it, but define in udpstream.h
class rs232Cassette; // Cassette forward-declaration.
class rs232CPM;      // CPM device.
class rs232Printer;  // Printer device

class virtualDevice
{
protected:
    friend systemBus;

    int _devnum;

    cmdFrame_t cmdFrame;
    bool listen_to_type3_polls = false;

    /**
     * @brief Send the desired buffer to the Atari.
     * @param buff The byte buffer to send to the Atari
     * @param len The length of the buffer to send to the Atari.
     * @return TRUE if the Atari processed the data in error, FALSE if the Atari successfully processed
     * the data.
     */
    void bus_to_computer(uint8_t *buff, uint16_t len, bool err);

    /**
     * @brief Receive data from the Atari.
     * @param buff The byte buffer provided for data from the Atari.
     * @param len The length of the amount of data to receive from the Atari.
     * @return An 8-bit wrap-around checksum calculated by the Atari, which should be checked with rs232_checksum()
     */
    uint8_t bus_to_peripheral(uint8_t *buff, uint16_t len);

    /**
     * @brief Send an acknowledgement byte to the Atari 'A'
     * This should be used if the command received by the RS232 device is valid, and is used to signal to the
     * Atari that we are now processing the command.
     */
    void rs232_ack();

    /**
     * @brief Send a non-acknowledgement (NAK) to the Atari 'N'
     * This should be used if the command received by the RS232 device is invalid, in the first place. It is not
     * the same as rs232_error().
     */
    void rs232_nak();

    /**
     * @brief Send a COMPLETE to the Atari 'C'
     * This should be used after processing of the command to indicate that we've successfully finished. Failure to send
     * either a COMPLETE or ERROR will result in a RS232 TIMEOUT (138) to be reported in DSTATS.
     */
    void rs232_complete();

    /**
     * @brief Send an ERROR to the Atari 'E'
     * This should be used during or after processing of the command to indicate that an error resulted
     * from processing the command, and that the Atari should probably re-try the command. Failure to
     * send an ERROR or COMPLTE will result in a RS232 TIMEOUT (138) to be reported in DSTATS.
     */
    void rs232_error();

    /**
     * @brief Return the aux bytes in cmdFrame as a single 16-bit or
     * 32-bit value, commonly used, for example to retrieve a sector
     * number, for disk, or a number of bytes waiting for the
     * rs232Network device.
     */
    // FIXME - these should probably be macros
    uint16_t rs232_get_aux16_lo();
    uint16_t rs232_get_aux16_hi();
    uint32_t rs232_get_aux32();

    /**
     * @brief All RS232 commands by convention should return a status command, using bus_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void rs232_status() = 0;

    /**
     * @brief All RS232 devices repeatedly call this routine to fan out to other methods for each command.
     * This is typcially implemented as a switch() statement.
     */
    virtual void rs232_process(cmdFrame_t *cmd_ptr) = 0;

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief get the RS232 device Number (1-255)
     * @return The device number registered for this device
     */
    int id() { return _devnum; };

    /**
     * @brief Command 0x3F '?' intended to return a single byte to the atari via bus_to_computer(), which
     * signifies the high speed RS232 divisor chosen by the user in their #FujiNet configuration.
     */
    virtual void rs232_high_speed();

    /**
     * @brief Is this virtualDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

    /**
     * @brief status wait counter
     */
    uint8_t status_wait_count = 5;
};

enum rs232_message : uint16_t
{
    RS232MSG_DISKSWAP,  // Rotate disk
    RS232MSG_DEBUG_TAPE // Tape debug msg
};

struct rs232_message_t
{
    rs232_message message_id;
    uint16_t message_arg;
};

// typedef rs232_message_t rs232_message_t;

class systemBus
{
private:
    std::forward_list<virtualDevice *> _daisyChain;

    int _command_frame_counter = 0;

    virtualDevice *_activeDev = nullptr;
    rs232Modem *_modemDev = nullptr;
    rs232Fuji *_fujiDev = nullptr;
    rs232Network *_netDev[8] = {nullptr};
    rs232UDPStream *_udpDev = nullptr;
    rs232CPM *_cpmDev = nullptr;
    rs232Printer *_printerdev = nullptr;

    int _rs232Baud = RS232_BAUDRATE;
    int _rs232BaudHigh = RS232_BAUDRATE;
    int _rs232BaudUltraHigh = RS232_BAUDRATE;

    bool useUltraHigh = false; // Use fujinet derived clock.

#if FUJINET_OVER_USB
    ACMChannel _port;
#else /* ! FUJINET_OVER_USB */
    UARTChannel _port;
#endif /* FUJINET_OVER_USB */

    void _rs232_process_cmd();
    /* void _rs232_process_queue(); */

public:
    void setup();
    void service();
    void shutdown();

    int numDevices();
    void addDevice(virtualDevice *pDevice, int device_id);
    void remDevice(virtualDevice *pDevice);
    virtualDevice *deviceById(int device_id);
    void changeDeviceId(virtualDevice *pDevice, int device_id);

    int getBaudrate();                                          // Gets current RS232 baud rate setting
    void setBaudrate(int baud);                                 // Sets RS232 to specific baud rate
    void toggleBaudrate();                                      // Toggle between standard and high speed RS232 baud rate

    int setHighSpeedIndex(int hrs232_index);                      // Set HRS232 index. Sets high speed RS232 baud and also returns that value.
    int getHighSpeedIndex();                                    // Gets current HRS232 index
    int getHighSpeedBaud();                                     // Gets current HRS232 baud

    void setUDPHost(const char *newhost, int port);             // Set new host/ip & port for UDP Stream
    void setUltraHigh(bool _enable, int _ultraHighBaud = 0);    // enable ultrahigh/set baud rate
    bool getUltraHighEnabled() { return useUltraHigh; }
    int getUltraHighBaudRate() { return _rs232BaudUltraHigh; }

    rs232Printer *getPrinter() { return _printerdev; }
    rs232CPM *getCPM() { return _cpmDev; }


    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    // Everybody thinks "oh I know how a serial port works, I'll just
    // access it directly and bypass the bus!" ಠ_ಠ
    size_t read(void *buffer, size_t length) { return _port.read(buffer, length); }
    size_t read() { return _port.read(); }
    size_t write(const void *buffer, size_t length) { return _port.write(buffer, length); }
    size_t write(int n) { return _port.write(n); }
    size_t available() { return _port.available(); }
    void flushOutput() { _port.flushOutput(); }
    size_t print(int n, int base = 10) { return _port.print(n, base); }
    size_t print(const char *str) { return _port.print(str); }
    size_t print(const std::string &str) { return _port.print(str); }
};

extern systemBus SYSTEM_BUS;

#endif // guard
