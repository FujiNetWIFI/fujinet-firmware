#ifndef COCO_H
#define COCO_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <forward_list>

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

// class def'ns
class drivewireModem;          // declare here so can reference it, but define in modem.h
class drivewireFuji;        // declare here so can reference it, but define in fuji.h
class systemBus;      // declare early so can be friend
class drivewireNetwork;     // declare here so can reference it, but define in network.h
class drivewireUDPStream;   // declare here so can reference it, but define in udpstream.h
class drivewireCassette;    // Cassette forward-declaration.
class drivewireCPM;         // CPM device.
class drivewirePrinter;     // Printer device

class virtualDevice
{
protected:
    friend systemBus;

    int _devnum;

    cmdFrame_t cmdFrame;
    bool listen_to_type3_polls = false;
    
    /**
     * @brief All DRIVEWIRE devices repeatedly call this routine to fan out to other methods for each command. 
     * This is typcially implemented as a switch() statement.
     */
    virtual void drivewire_process(uint32_t commanddata, uint8_t checksum) = 0;

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief get the DRIVEWIRE device Number (1-255)
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

enum drivewire_message : uint16_t
{
    DRIVEWIREMSG_DISKSWAP,  // Rotate disk
    DRIVEWIREMSG_DEBUG_TAPE // Tape debug msg
};

struct drivewire_message_t
{
    drivewire_message message_id;
    uint16_t message_arg;
};

// typedef drivewire_message_t drivewire_message_t;

class systemBus
{
private:
    std::forward_list<virtualDevice *> _daisyChain;

    int _command_frame_counter = 0;

    virtualDevice *_activeDev = nullptr;
    drivewireModem *_modemDev = nullptr;
    drivewireFuji *_fujiDev = nullptr;
    drivewireNetwork *_netDev[8] = {nullptr};
    drivewireUDPStream *_udpDev = nullptr;
    drivewireCassette *_cassetteDev = nullptr;
    drivewireCPM *_cpmDev = nullptr;
    drivewirePrinter *_printerdev = nullptr;

    bool useUltraHigh = false; // Use fujinet derived clock.

    void _drivewire_process_cmd();
    void _drivewire_process_queue();

    /**
     * @brief Current Baud Rate
     */
    int _drivewireBaud;

public:
    void setup();
    void service();
    void shutdown();

    int numDevices();
    void addDevice(virtualDevice *pDevice, int device_id);
    void remDevice(virtualDevice *pDevice);
    virtualDevice *deviceById(int device_id);
    void changeDeviceId(virtualDevice *pDevice, int device_id);

    int getBaudrate();                                          // Gets current DRIVEWIRE baud rate setting
    void setBaudrate(int baud);                                 // Sets DRIVEWIRE to specific baud rate
    void toggleBaudrate();                                      // Toggle between standard and high speed DRIVEWIRE baud rate

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    drivewireCassette *getCassette() { return _cassetteDev; }
    drivewirePrinter *getPrinter() { return _printerdev; }
    drivewireCPM *getCPM() { return _cpmDev; }

    // I wish this codebase would make up its mind to use camel or snake casing.
    drivewireModem *get_modem() { return _modemDev; }

    QueueHandle_t qDrivewireMessages = nullptr;
};

extern systemBus DRIVEWIRE;

#endif // guard
