//
// http://www.boisypitre.com/retrocomputing/drivewire/
// https://www.frontiernet.net/~mmarlette/Cloud-9/Hardware/DriveWire3.html
// https://www.cocopedia.com/wiki/index.php/DRIVEWIRE.ZIP
//
// https://sourceforge.net/projects/drivewireserver/
// https://github.com/qbancoffee/drivewire4
// https://github.com/n6il/toolshed/tree/master/hdbdos
//
// https://github.com/MyTDT-Mysoft/COCO-FastLoader
//
// https://www.cocopedia.com/wiki/index.php/Main_Page
// https://github.com/qbancoffee/coco_motherboards
// https://archive.worldofdragon.org/index.php?title=Main_Page
// https://sites.google.com/site/dabarnstudio/drivewire-4-3-4e
// https://sites.google.com/site/dabarnstudio/coco-midi-drivewire
//

#ifndef COCO_H
#define COCO_H

#include "bus.h"
#include "FujiDWPacket.h"
#include "opcode.h"
#include "BoIPChannel.h"
#include "UARTChannel.h"
#include "ACMChannel.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#include "status_error_codes.h"
#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
#include "../rs232/FujiBusPacket.h"
#include <deque>
#endif

#ifdef ESP32_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif

#include <forward_list>
#include <map>
#include <cassert>
#include "media.h"

#define FUJI_COMMAND_PACKET FujiDWPacket

#define DRIVEWIRE_BAUDRATE 57600

#if !defined(ESP_PLATFORM) || \
    (FN_UART_BUS == UART_NUM_1 && defined(PIN_UART1_RX)) ||     \
    (FN_UART_BUS == UART_NUM_2 && defined(PIN_UART2_RX))
#undef FUJINET_OVER_USB
#else
#define FUJINET_OVER_USB 1
#endif

#define FEATURE_EMCEE    0x01
#define FEATURE_DLOAD    0x02
#define FEATURE_HDBDOS   0x04
#define FEATURE_DOSPLUS  0x08
#define FEATURE_PRINTER  0x10
#define FEATURE_SSH      0x20
#define FEATURE_PLAYSND  0x40
#define FEATURE_RESERVED 0x80

#define DWINIT_FEATURES  FEATURE_DLOAD | \
                         FEATURE_HDBDOS | \
                         FEATURE_PRINTER

// class def'ns
class drivewireModem;     // declare here so can reference it, but define in modem.h
class drivewireFuji;      // declare here so can reference it, but define in fuji.h
class systemBus;          // declare early so can be friend
class drivewireNetwork;   // declare here so can reference it, but define in network.h
class drivewireNetStream; // declare here so can reference it, but define in netstream.h
class drivewireCassette;  // Cassette forward-declaration.
class drivewireCPM;       // CPM device.
class drivewirePrinter;   // Printer device
class fujiDevice;

class drivewireDevice
{
    friend systemBus;
    friend fujiDevice;

protected:
    nDevStatus_t _errorCode;
    fujiDeviceID_t _devnum;

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

    fujiDeviceID_t id() { return _devnum; };
};

class virtualDevice : public drivewireDevice
{
public:
    virtual bool processCommand(const FujiDWPacket &packet) = 0;
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

class systemBus : public SystemBusBase
{
    friend FujiDWPacket;

private:
    IOChannel *_port = nullptr;
#if FUJINET_OVER_USB
    ACMChannel _serial;
#else /* ! FUJINET_OVER_USB */
    UARTChannel _serial;
#endif /* FUJINET_OVER_USB */
    BoIPChannel _becker;

#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
    std::deque<uint8_t> _dbc_pushback;
#endif

#if FUJINET_OVER_USB
    // USB servicing runs boosted through the cold-boot WiFi association storm,
    // then drops to normal once the station associates (see setup()/service()).
    bool _usb_boot_priority = false;
#endif

    const FujiDWPacket *_activeFrame;
    drivewireDevice *_activeDev = nullptr;
    drivewireModem *_modemDev = nullptr;
    drivewireFuji *_fujiDev = nullptr;
    //drivewireNetwork *_netDev[8] = {nullptr};
    drivewireNetStream *_streamDev = nullptr;
    drivewireCassette *_cassetteDev = nullptr;
    drivewireCPM *_cpmDev = nullptr;
    drivewirePrinter *_printerdev = nullptr;
    FILE *pNamedObjFp;
    uint8_t szNamedMount[256];
    uint8_t bDragon;

    ByteBuffer _transaction_response;
    bool _transaction_handle_command(const FujiDWPacket &packet, virtualDevice &device);

    void _drivewire_process_cmd();
    void _drivewire_process_queue();

#ifdef ESP_PLATFORM
    void configureGPIO();
    int readBaudSwitch();
#endif /* ESP_PLATFORM */

    /**
     * @brief Current Baud Rate
     */
    int _drivewireBaud = DRIVEWIRE_BAUDRATE;

    /**
     * @brief Logical sector number (1-16777216)
     */
    uint32_t lsn;

    /**
     * @brief Drive number (0-255)
     */
    uint8_t drive_num;

    /**
     * @brief Sector data (256 bytes)
     */
    uint8_t sector_data[MEDIA_BLOCK_SIZE];

    /**
     * @brief NOP command (do nothing)
     */
    void op_jeff();
    void op_nop();
    void op_reset();
    void op_readex();
    void op_fuji(dwOpcode_t opcode);
    void op_net(dwOpcode_t opcode);
    void op_cpm(dwOpcode_t opcode);
    void op_clock(dwOpcode_t opcode);
    void op_write();
    void op_time();
    void op_init();
    void op_serinit();
    void op_serterm();
    void op_dwinit();
    void op_unhandled(dwOpcode_t opcode);
    void op_getstat();
    void op_setstat();
    void op_sergetstat();
    void op_sersetstat();
    void op_serread();
    void op_serreadm();
    void op_serwrite();
    void op_serwritem();
    void op_print();
    void op_namedobj_mnt();

    size_t read(void *buffer, size_t length) {
#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
        size_t n = 0;
        while (!_dbc_pushback.empty() && n < length) {
            ((uint8_t *)buffer)[n++] = _dbc_pushback.front();
            _dbc_pushback.pop_front();
        }
        if (n < length)
            n += _port->read((uint8_t *)buffer + n, length - n);
        return n;
#else
        return _port->read(buffer, length);
#endif
    }
    int read() {
        uint8_t b;
        return read(&b, 1) == 1 ? b : -1;
    }
    size_t write(const void *buffer, size_t length) { return _port->write(buffer, length); }
    size_t write(int n) { return _port->write(n); }
    size_t available() {
#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
        return _dbc_pushback.size() + _port->available();
#else
        return _port->available();
#endif
    }
    void flushOutput() { _port->flushOutput(); }

public:
    void setup();
    void service();
    void shutdown();

    void transaction_accept(transState_t expectMoreData) override;
    void transaction_success() override;
    void transaction_error() override;
    success_is_true transaction_get(void *data, size_t len) override;
    using SystemBusBase::transaction_send;
    void transaction_send(const void *data, size_t len, bool is_error=false) override;

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };
    bool motorActive = false;
    bool isDragon() { return bDragon; }

    // When true, op_readex()'s named-object fallback opens /DGNLOBBY.DWL
    // instead of /AUTOLOAD.DWL. Set via FUJICMD_SET_BOOT_MODE mode 2 on Dragon.
    bool useLobbyDwl = false;

    drivewireCassette *getCassette() { return _cassetteDev; }
    drivewirePrinter *getPrinter() { return _printerdev; }
    void setPrinter(drivewirePrinter *_p) { _printerdev = _p; }
    drivewireCPM *getCPM() { return _cpmDev; }
    std::map<uint8_t,drivewireNetwork *> _netDev;

    // I wish this codebase would make up its mind to use camel or snake casing.
    drivewireModem *get_modem() { return _modemDev; }

#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
    std::unique_ptr<FujiBusPacket> readBusPacket(int first = -1);
    void writeBusPacket(FujiBusPacket &packet);

    template<typename... Args>
    std::unique_ptr<FujiBusPacket> sendCommand(fujiDeviceID_t device,
                                               fujiCommandID_t command,
                                               Args&&... args)
    {
        FujiBusPacket packet(device, command, std::forward<Args>(args)...);
        writeBusPacket(packet);
        return readBusPacket();
    }

    std::unique_ptr<FujiBusPacket> sendCommand(fujiDeviceID_t device,
                                               fujiCommandID_t command,
                                               void *buf, size_t len)
    {
        std::string data(reinterpret_cast<const char *>(buf), static_cast<size_t>(len));
        return sendCommand(device, command, std::move(data));
    }
#endif /* PINMAP_FUJIVERSAL_DRIVEWIRE */

#ifdef ESP32_PLATFORM
    QueueHandle_t qDrivewireMessages = nullptr;
#endif

    /* BoIP things */
    bool isBoIP() { return _port == &_becker; }
    void setHost(const char *host, int port) { _becker.setHost(host, port); }
    void selectSerialPort(bool useSerial) {
        if (useSerial)
            _port = &_serial;
        else
            _port = &_becker;
    }

    // For compatibility with fujiDevice.cpp
    void changeDeviceId(void *pDevice, int device_id) {};
};

extern systemBus SYSTEM_BUS;

#endif // guard
