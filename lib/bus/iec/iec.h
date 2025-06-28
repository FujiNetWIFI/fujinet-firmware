#ifndef IEC_H
#define IEC_H

#include <cstdint>
#include <forward_list>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <utility>
#include <string>
#include <map>
#include <queue>
#include <memory>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "fnSystem.h"

#include <soc/gpio_reg.h>

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

//#include "IECHost.h"
#include "IECDevice.h"
#include "IECBusHandler.h"

#define BUS_DEVICEID_PRINTER 4
#define BUS_DEVICEID_DISK 8
#define BUS_DEVICEID_NETWORK 16


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
 * @class systemBus
 * @brief the system bus that all virtualDevices attach to.
 */
class systemBus : public IECBusHandler
{
public:
    systemBus();

    /**
     * @brief called in main.cpp to set up the bus.
     */
    void setup();

    /**
     * @brief Run one iteration of the bus service loop
     */
    void service();

    /**
     * @brief called from main shutdown to clean up the device.
     */
    void shutdown();

    /**
     * @brief Are we shutting down?
     * @return value of shuttingDown
     */
    bool getShuttingDown() { return shuttingDown; }

 private:
    /**
     * @brief is device shutting down?
     */
    bool shuttingDown = false;

};
/**
 * @brief Return
 */
extern systemBus IEC;

#endif /* IEC_H */
