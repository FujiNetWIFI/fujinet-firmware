#ifndef COMLYNX_H
#define COMLYNX_H

/**
 * Comlynx Routines
 */

#include "bus.h"
#include "FujiLynxPacket.h"
#include "global_types.h"
#include "UARTChannel.h"
#include "BoIPChannel.h"
#include "fujiDeviceID.h"
#include "fujiCommandID.h"
#ifdef ESP_PLATFORM
#include <freertos/queue.h>
#endif /* ESP_PLATFORM */

#include <forward_list>
#include <map>

#define FUJI_COMMAND_PACKET FujiLynxPacket

#define COMLYNX_BAUDRATE 62500
#define COMLYNX_IDLE_TIME 500

#define COMLYNX_RESET_DEBOUNCE_PERIOD 100 // in ms

class systemBus;
class lynxFuji;     // declare here so can reference it, but define in fuji.h
class lynxPrinter;
class lynxNetStream;
class lynxNetwork;
class fujiDevice;

/**
 * @brief An Comlynx Device
 */
class virtualDevice
{
    friend systemBus; // We exist on the Comlynx Bus, and need to let it much with our internals
    friend fujiDevice;

protected:
    virtual void reset();
    virtual void shutdown() {}
    virtual void comlynx_process(const FujiLynxPacket &packet);

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
    fujiDeviceID_t id();
};

/**
 * @brief The Comlynx Bus
 */
class systemBus : public SystemBusBase
{
private:
    virtualDevice *_activeDev = nullptr;
    const FujiLynxPacket *_activePacket;
    lynxNetStream *_streamDev = nullptr;

    IOChannel *_port;
    UARTChannel _serial;
    BoIPChannel _boip;

    void _comlynx_process_cmd();
    void _comlynx_process_queue();

public:
    void setup();
    void service();
    void shutdown();
    void reset();

    void change_baud(int32_t baud);

    /**
     * @brief Wait to see if Comlynx bus is idle.
     */
    bool wait_for_idle();
    bool netstreamActive() const;

    void addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id) override;

    void setStreamHost(const char *newhost, int port);
    void setStreamHostWithOptions(const char *newhost, int port, int mode, bool register_enabled, bool redeye_enabled);

    void setRedeyeMode(bool enable);
    void setRedeyeGameRemap(uint32_t remap);

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };

    void transaction_accept(transState_t expectMoreData) override;
    void transaction_success() override;
    void transaction_error() override;
    using SystemBusBase::transaction_get;
    success_is_true transaction_get(void *data, size_t len) override;
    using SystemBusBase::transaction_send;
    void transaction_send(const void *data, size_t len, bool is_error=false) override;

    void writeBusPacket(const FujiLynxPacket &packet);
    void sendAckPacket();
    void sendNakPacket();

    // Everybody thinks "oh I know how a serial port works, I'll just
    // access it directly and bypass the bus!" ಠ_ಠ
    size_t read(void *buffer, size_t length) { return _port->read(buffer, length); }
    size_t read() { return _port->read(); }
    size_t write(const void *buffer, size_t length) { return _port->write(buffer, length); }
    size_t write(int n) { return _port->write(n); }
    size_t available() { return _port->available(); }
    void flush() { _port->flushOutput(); }
    size_t print(int n, int base = 10) { return _port->print(n, base); }
    size_t print(const char *str) { return _port->print(str); }
    size_t print(const std::string &str) { return _port->print(str); }
};

extern systemBus SYSTEM_BUS;

#endif /* COMLYNX_H */
