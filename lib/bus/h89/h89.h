#ifndef H89_H
#define H89_H

/**
 * H89 Routines
 */

#include "cmdFrame.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>

#include <map>

class systemBus;
class H89Fuji;     // declare here so can reference it, but define in fuji.h
class H89CPM;
class H89Modem;
class H89Printer;

/**
 * @brief An H89 Device
 */
class virtualDevice
{
protected:
    friend systemBus; // We exist on the H89 Bus, and need its methods.

    /**
     * @brief Device Number: 0-255
     */
    uint8_t _devnum;

    virtual void shutdown() {}

    /**
     * @brief Perform reset of device
     */
    virtual void reset() {};

    /**
     * @brief All H89 devices repeatedly call this routine to fan out to other methods for each command.
     * This is typcially implemented as a switch() statement.
     */
    virtual void process(uint32_t commanddata, uint8_t checksum) = 0;

    /**
     * @brief send current status of device
     */
    virtual void status() {};



    /**
     * @brief command frame, used by network protocol, ultimately
     */
    cmdFrame_t cmdFrame;

public:

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
 * @brief The H89 Bus
 */
class systemBus
{
private:
    std::map<uint8_t, virtualDevice *> _daisyChain;

public:
    void setup(); // one time setup
    void service(); // this runs in a loop
    void shutdown(); // shutdown
    void reset(); // reset

    int numDevices();
    void addDevice(virtualDevice *pDevice, FujiDeviceID device_id);
    void remDevice(virtualDevice *pDevice);
    void remDevice(FujiDeviceID device_id);
    bool deviceExists(FujiDeviceID device_id);
    void enableDevice(FujiDeviceID device_id);
    void disableDevice(FujiDeviceID device_id);
    bool enabledDeviceStatus(FujiDeviceID device_id);
    virtualDevice *deviceById(FujiDeviceID device_id);
    void changeDeviceId(virtualDevice *pDevice, FujiDeviceID device_id);
    QueueHandle_t qH89Messages = nullptr;

    bool shuttingDown = false;                                  // TRUE if we are in shutdown process
    bool getShuttingDown() { return shuttingDown; };
};

extern systemBus SYSTEM_BUS;

#endif /* H89_H */
