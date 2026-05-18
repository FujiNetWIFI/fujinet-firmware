#ifndef RS232_H
#define RS232_H

#include "bus.h"
#include "UARTChannel.h"
#include "ACMChannel.h"
#include "FujiBusPacket.h"
#include "../drivewire/BeckerSocket.h"
#include "global_types.h"

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif /* ESP_PLATFORM */

#include <forward_list>

#define RS232_BAUDRATE 115200

#if !defined(ESP_PLATFORM) || \
    (FN_UART_BUS == UART_NUM_1 && defined(PIN_UART1_RX)) ||     \
    (FN_UART_BUS == UART_NUM_2 && defined(PIN_UART2_RX))
#undef FUJINET_OVER_USB
#else
#define FUJINET_OVER_USB 1
#endif

enum FujiStatusReq {
    STATUS_NETWORK_CONNERR = 0,
    STATUS_NETWORK_IP      = 1,
    STATUS_NETWORK_NETMASK = 2,
    STATUS_NETWORK_GATEWAY = 3,
    STATUS_NETWORK_DNS     = 4,

    STATUS_MOUNT_TIME      = 1,
};

// helper functions
uint8_t rs232_checksum(uint8_t *buf, unsigned short len);

// class def'ns
class rs232Modem;    // declare here so can reference it, but define in modem.h
class rs232Fuji;     // declare here so can reference it, but define in fuji.h
class systemBus;      // declare early so can be friend
class rs232Network;  // declare here so can reference it, but define in network.h
class rs232NetStream; // declare here so can reference it, but define in netstream.h
class rs232Cassette; // Cassette forward-declaration.
class rs232CPM;      // CPM device.
class rs232Printer;  // Printer device
class fujiDevice;

class virtualDevice
{
    friend systemBus;
    friend fujiDevice;

protected:
    fujiDeviceID_t _devnum;

    bool listen_to_type3_polls = false;

    transState_t _transaction_state = TRANS_STATE::INVALID;
    virtual void transaction_begin(transState_t expectMoreData);
    virtual void transaction_complete();
    virtual void transaction_error();
    virtual success_is_true transaction_get(void *data, size_t len);
    virtual void transaction_put(const void *data, size_t len, bool err);

    // FIXME - This is a terrible hack to allow devices to continue to
    // use the pattern of fetching data on their own instead of
    // upgrading them fully to work with packets.
    FujiBusPacket *_legacyPacketData;
    size_t _legacyDataPosition;

    /**
     * @brief All RS232 commands by convention should return a status command, using bus_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void rs232_status(FujiStatusReq reqType) = 0;

    /**
     * @brief All RS232 devices repeatedly call this routine to fan out to other methods for each command.
     * This is typcially implemented as a switch() statement.
     */
    virtual void rs232_process(FujiBusPacket &packet) = 0;

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief get the RS232 device Number (1-255)
     * @return The device number registered for this device
     */
    fujiDeviceID_t id() { return _devnum; };

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
    rs232NetStream *_streamDev = nullptr;
    rs232CPM *_cpmDev = nullptr;
    rs232Printer *_printerdev = nullptr;

    int _rs232Baud = RS232_BAUDRATE;

    IOChannel *_port;
#if FUJINET_OVER_USB
    ACMChannel _serial;
#else /* ! FUJINET_OVER_USB */
    UARTChannel _serial;
#endif /* FUJINET_OVER_USB */
    BeckerSocket _becker;

    void _rs232_process_cmd();
    /* void _rs232_process_queue(); */

public:
    void setup();
    void service();
    void shutdown();

    int numDevices();
    void addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id);
    void remDevice(virtualDevice *pDevice);
    virtualDevice *deviceById(fujiDeviceID_t device_id);
    void changeDeviceId(virtualDevice *pDevice, int device_id);

    int getBaudrate();                                          // Gets current RS232 baud rate setting
    void setBaudrate(int baud);                                 // Sets RS232 to specific baud rate

    void setStreamHost(const char *newhost, int port);             // Set new host/ip & port for NetStream

    rs232Printer *getPrinter() { return _printerdev; }
    rs232CPM *getCPM() { return _cpmDev; }


    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    std::unique_ptr<FujiBusPacket> readBusPacket(int first=-1);
    void writeBusPacket(FujiBusPacket &packet);
    void sendReplyPacket(fujiDeviceID_t source, bool ack, const void *data, size_t length);
    template<typename... Args>
    std::unique_ptr<FujiBusPacket> sendCommand(fujiDeviceID_t device,
                                               fujiCommandID_t command,
                                               Args&&... args)
    {
        FujiBusPacket packet(device, command, std::forward<Args>(args)...);
        writeBusPacket(packet);
        return readBusPacket();
    }

    // Convenience wrapper: raw buffer
    std::unique_ptr<FujiBusPacket> sendCommand(fujiDeviceID_t device,
                                               fujiCommandID_t command,
                                               void *buf, size_t len)
    {
        std::string data(reinterpret_cast<const char*>(buf), static_cast<size_t>(len));
        return sendCommand(device, command, std::move(data));
    }
};

extern systemBus SYSTEM_BUS;

#endif /* RS232_H */
