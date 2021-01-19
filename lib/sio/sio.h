#ifndef SIO_H
#define SIO_H

#include <forward_list>
#include "fnSystem.h"

// Pin configurations
#define PIN_INT 26
#define PIN_PROC 22
#define PIN_CKO 32
#define PIN_CKI 27
#define PIN_MTR 36
#define PIN_CMD 39

#define DELAY_T4 850
#define DELAY_T5 250


/*
Examples of values that can be defined in PLATFORMIO.INI
First number is calculated based on the index, second is what the ESP32 actually reports

FN_HISPEED_INDEX=0 // 124,017 (124,018) baud
FN_HISPEED_INDEX=1 // 108,800 (108,806) baud
FN_HISPEED_INDEX=2 //  96,909 (96,910) baud
FN_HISPEED_INDEX=3 //  87,361 (87,366) baud
FN_HISPEED_INDEX=4 //  79,526 (79,527) baud
FN_HISPEED_INDEX=5 //  72,981 (72,984) baud
FN_HISPEED_INDEX=6 //  67,431 (67,432) baud
FN_HISPEED_INDEX=7 //  62,665 (62,665) baud
FN_HISPEED_INDEX=8 //  58,529 (58,530) baud
FN_HISPEED_INDEX=9 //  54,905 (54,907) baud
FN_HISPEED_INDEX=10 //  51,703 (51,704) baud
FN_HISPEED_INDEX=20 //  32,660 (32,660) baud
FN_HISPEED_INDEX=30 //  23,868 (23,868) baud
FN_HISPEED_INDEX=38 //  19,639 (19,640) baud
FN_HISPEED_INDEX=40 //  18,806 (18,806) baud
*/

// The High speed SIO index
#ifndef FN_HISPEED_INDEX
#define SIO_HISPEED_INDEX 0x06
#else
#define SIO_HISPEED_INDEX FN_HISPEED_INDEX
#endif

#define SIO_ATARI_PAL_FREQUENCY 1773447
#define SIO_ATARI_NTSC_FREQUENCY 1789790

// We calculate this dynamically now in sioBus::setHighSpeedIndex()
// #define SIO_HISPEED_BAUDRATE ((SIO_ATARI_PAL_FREQUENCY * 10) / (10 * (2 * (SIO_HISPEED_INDEX + 7)) + 3))

#define SIO_STANDARD_BAUDRATE 19200

#define SIO_HISPEED_LOWEST_INDEX 0x0A // Lowest HSIO index we'll accept

#define COMMAND_FRAME_SPEED_CHANGE_THRESHOLD 2
#define SERIAL_TIMEOUT 300

#define SIO_DEVICEID_DISK 0x31
#define SIO_DEVICEID_DISK_LAST 0x3F

#define SIO_DEVICEID_PRINTER 0x40
#define SIO_DEVICEID_PRINTER_LAST 0x43

#define SIO_DEVICEID_FN_VOICE 0x43

#define SIO_DEVICEID_APETIME 0x45

#define SIO_DEVICEID_TYPE3POLL 0x4F

#define SIO_DEVICEID_RS232 0x50
#define SIO_DEVICEID_RS2323_LAST 0x53

#define SIO_DEVICEID_CASSETTE 0x5F

#define SIO_DEVICEID_FUJINET 0x70
#define SIO_DEVICEID_FN_NETWORK 0x71
#define SIO_DEVICEID_FN_NETWORK_LAST 0x78

#define SIO_DEVICEID_MIDI 0x99

// Not used, but for reference:
#define SIO_DEVICEID_SIO2BT_NET 0x4E
#define SIO_DEVICEID_SIO2BT_SMART 0x45 // Doubles as APETime and "High Score Submission" to URL
#define SIO_DEVICEID_APE 0x45
#define SIO_DEVICEID_ASPEQT 0x46
#define SIO_DEVICEID_PCLINK 0x6F

#define SIO_DEVICEID_CPM 0x5A

union cmdFrame_t {
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

//helper functions
uint8_t sio_checksum(uint8_t *buf, unsigned short len);

// class def'ns
class sioModem;   // declare here so can reference it, but define in modem.h
class sioFuji;    // declare here so can reference it, but define in fuji.h
class sioBus;     // declare early so can be friend
class sioNetwork; // declare here so can reference it, but define in network.h
class sioMIDIMaze;   // declare here so can reference it, but define in midimaze.h
class sioCassette;  // Cassette forward-declaration.
class sioCPM;    // CPM device.
class sioPrinter; // Printer device

class sioDevice
{
protected:
    friend sioBus;

    int _devnum;

    cmdFrame_t cmdFrame;
    bool listen_to_type3_polls = false;
    uint8_t status_wait_count = 5;

    /**
     * @brief Send the desired buffer to the Atari.
     * @param buff The byte buffer to send to the Atari
     * @param len The length of the buffer to send to the Atari.
     * @return TRUE if the Atari processed the data in error, FALSE if the Atari successfully processed
     * the data.
     */
    void sio_to_computer(uint8_t *buff, uint16_t len, bool err);

    /**
     * @brief Receive data from the Atari.
     * @param buff The byte buffer provided for data from the Atari.
     * @param len The length of the amount of data to receive from the Atari.
     * @return An 8-bit wrap-around checksum calculated by the Atari, which should be checked with sio_checksum()
     */
    uint8_t sio_to_peripheral(uint8_t *buff, uint16_t len);

    /**
     * @brief Send an acknowledgement byte to the Atari 'A'
     * This should be used if the command received by the SIO device is valid, and is used to signal to the
     * Atari that we are now processing the command.
     */
    void sio_ack();

    /**
     * @brief Send a non-acknowledgement (NAK) to the Atari 'N'
     * This should be used if the command received by the SIO device is invalid, in the first place. It is not
     * the same as sio_error().
     */
    void sio_nak();

    /**
     * @brief Send a COMPLETE to the Atari 'C'
     * This should be used after processing of the command to indicate that we've successfully finished. Failure to send
     * either a COMPLETE or ERROR will result in a SIO TIMEOUT (138) to be reported in DSTATS.
     */
    void sio_complete();

    /**
     * @brief Send an ERROR to the Atari 'E'
     * This should be used during or after processing of the command to indicate that an error resulted
     * from processing the command, and that the Atari should probably re-try the command. Failure to
     * send an ERROR or COMPLTE will result in a SIO TIMEOUT (138) to be reported in DSTATS.
     */
    void sio_error();

    /**
     * @brief Return the two aux bytes in cmdFrame as a single 16-bit value, commonly used, for example to retrieve
     * a sector number, for disk, or a number of bytes waiting for the sioNetwork device.
     * 
     * @return 16-bit value of DAUX1/DAUX2 in cmdFrame.
     */
    unsigned short sio_get_aux();

    /**
     * @brief All SIO commands by convention should return a status command, using sio_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void sio_status() = 0;

    /**
     * @brief All SIO devices repeatedly call this routine to fan out to other methods for each command. 
     * This is typcially implemented as a switch() statement.
     */
    virtual void sio_process(uint32_t commanddata, uint8_t checksum) = 0;

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown() {};

public:
    /**
     * @brief get the SIO device Number (1-255)
     * @return The device number registered for this device
     */
    int id() { return _devnum; };

    /**
     * @brief Command 0x3F '?' intended to return a single byte to the atari via sio_to_computer(), which
     * signifies the high speed SIO divisor chosen by the user in their #FujiNet configuration.
     */
    virtual void sio_high_speed();

    /**
     * @brief Is this sioDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

    /**
     * @brief Get the sioBus object that this sioDevice is attached to.
     */
    sioBus sio_get_bus();
};

enum sio_message : uint16_t
{
    SIOMSG_DISKSWAP,            // Rotate disk
    SIOMSG_DEBUG_TAPE           // Tape debug msg
};

struct sio_message_t
{
    sio_message message_id;
    uint16_t message_arg;
};

// typedef sio_message_t sio_message_t;

class sioBus
{
private:
    std::forward_list<sioDevice *> _daisyChain;

    int _command_frame_counter = 0;

    sioDevice *_activeDev = nullptr;
    sioModem *_modemDev = nullptr;
    sioFuji *_fujiDev = nullptr;
    sioNetwork *_netDev[8] = { nullptr };
    sioMIDIMaze *_midiDev = nullptr;
    sioCassette *_cassetteDev = nullptr;
    sioCPM *_cpmDev = nullptr;
    sioPrinter *_printerdev = nullptr;

    int _sioBaud = SIO_STANDARD_BAUDRATE;
    int _sioHighSpeedIndex = SIO_HISPEED_INDEX;
    int _sioBaudHigh;
    int _sioBaudUltraHigh;

    bool useUltraHigh=false; // Use fujinet derived clock.

    void _sio_process_cmd();
    void _sio_process_queue();

public:

    void setup();
    void service();
    void shutdown();

    int numDevices();
    void addDevice(sioDevice *pDevice, int device_id);
    void remDevice(sioDevice *pDevice);
    sioDevice *deviceById(int device_id);
    void changeDeviceId(sioDevice *pDevice, int device_id);

    int getBaudrate(); // Gets current SIO baud rate setting
    void setBaudrate(int baud); // Sets SIO to specific baud rate
    void toggleBaudrate(); // Toggle between standard and high speed SIO baud rate

    int setHighSpeedIndex(int hsio_index); // Set HSIO index. Sets high speed SIO baud and also returns that value.
    int getHighSpeedIndex(); // Gets current HSIO index
    int getHighSpeedBaud(); // Gets current HSIO baud

    void setMIDIHost(const char *newhost); // Set new host/ip for MIDIMaze
    void setUltraHigh(bool _enable, int _ultraHighBaud = 0); // enable ultrahigh/set baud rate
    bool getUltraHighEnabled() { return useUltraHigh; }
    int getUltraHighBaudRate() { return _sioBaudUltraHigh; } 

    sioCassette* getCassette() { return _cassetteDev; }
    sioPrinter* getPrinter() { return _printerdev; }
    sioCPM* getCPM() { return _cpmDev; }

    QueueHandle_t qSioMessages = nullptr;
};

extern sioBus SIO;

#endif // guard
