#ifndef IEC_H
#define IEC_H

// This code uses code from the Meatloaf Project:
// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#include <cstdint>
#include <forward_list>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <utility>
#include <string>
#include <map>
#include <queue>
#include <driver/gpio.h>
#include "fnSystem.h"
#include "protocol/iecProtocolBase.h"

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

// Return values for service:
typedef enum
{
    BUS_OFFLINE = -3, // Bus is empty
    BUS_RESET = -2,   // The bus is in a reset state (RESET line).
    BUS_ERROR = -1,   // A problem occoured, reset communication
    BUS_IDLE = 0,     // Nothing recieved of our concern
    BUS_ACTIVE = 1,   // ATN is pulled and a command byte is expected
    BUS_PROCESS = 2,  // A command is ready to be processed
} bus_state_t;

/**
 * @enum bus command
 */
typedef enum
{
    IEC_GLOBAL = 0x00,    // 0x00 + cmd (global command)
    IEC_LISTEN = 0x20,    // 0x20 + device_id (LISTEN) (0-30)
    IEC_UNLISTEN = 0x3F,  // 0x3F (UNLISTEN)
    IEC_TALK = 0x40,      // 0x40 + device_id (TALK) (0-30)
    IEC_UNTALK = 0x5F,    // 0x5F (UNTALK)
    IEC_REOPEN = 0x60,    // 0x60 + channel (OPEN CHANNEL) (0-15)
    IEC_REOPEN_JD = 0x61, // 0x61 + channel (OPEN CHANNEL) (0-15) - JIFFYDOS LOAD
    IEC_CLOSE = 0xE0,     // 0xE0 + channel (CLOSE NAMED CHANNEL) (0-15)
    IEC_OPEN = 0xF0       // 0xF0 + channel (OPEN NAMED CHANNEL) (0-15)
} bus_command_t;

typedef enum
{
    DEVICE_ERROR = -1,
    DEVICE_IDLE = 0, // Ready and waiting
    DEVICE_ACTIVE = 1,
    DEVICE_LISTEN = 2,  // A command is recieved and data is coming to us
    DEVICE_TALK = 3,    // A command is recieved and we must talk now
    DEVICE_PROCESS = 4, // Execute device command
} device_state_t;

/**
 * @class IECData
 * @brief the IEC command data passed to devices
 */
class IECData
{
public:
    /**
     * @brief the primary command byte
     */
    uint8_t primary = 0;
    /**
     * @brief the primary device number
     */
    uint8_t device = 0;
    /**
     * @brief the secondary command byte
     */
    uint8_t secondary = 0;
    /**
     * @brief the secondary command channel
     */
    uint8_t channel = 0;
    /**
     * @brief the device command
     */
    std::string payload = "";

    /**
     * @brief clear and initialize IEC command data
     */
    void init(void)
    {
        primary = 0;
        device = 0;
        secondary = 0;
        channel = 0;
        payload = "";
    }
};

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
     * @brief The current device command
     */
    std::string payload;

    /**
     * @brief pointer to the current command data
     */
    IECData *commanddata;

    /**
     * @brief current device state.
     */
    device_state_t device_state;

    /**
     * @brief response queue (e.g. INPUT)
     */
    std::queue<std::string> response_queue;

    /**
     * @brief Get device ready to handle next phase of command.
     */
    device_state_t queue_command(IECData *data)
    {
        if (data->primary == IEC_LISTEN)
            device_state = DEVICE_LISTEN;
        else if (data->primary == IEC_TALK)
            device_state = DEVICE_TALK;

        return device_state;
    }

    /**
     * @brief All IEC commands by convention should return a status command, using bus_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void status() = 0;

    /**
     * @brief All IEC devices repeatedly call this routine to fan out to other methods for each command.
     *        This is typcially implemented as a switch() statement.
     * @param commanddata The command data structure to pass
     * @return new device state.
     */
    virtual device_state_t process(IECData *commanddata);

    /**
     * @brief Dump the current IEC frame to terminal.
     */
    void dumpData();

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief get the IEC device Number (1-31)
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
     * @brief the active bus protocol
     */
    IecProtocolBase *protocol = NULL;

    /**
     * IEC LISTEN received
     */
    bus_state_t deviceListen();

    /**
     * IEC TALK requested
     */
    bus_state_t deviceTalk();

    /**
     * BUS TURNAROUND (act like listener)
     */
    bool turnAround();

    /**
     * Done with turnaround, go back to being talker.
     */
    bool undoTurnAround();

    /**
     * @brief called to process the next command
     */
    void process_cmd();

    /**
     * @brief called to process a queue item (such as disk swap)
     */
    void process_queue();

    /**
     * @brief Release the bus lines, we're done.
     */
    void releaseLines(bool wait = false);

public:
    /**
     * @brief bus flags
     */
    uint16_t flags = CLEAR;

    /**
     * @brief current bus state
     */
    bus_state_t bus_state;

    /**
     * @brief data about current bus transaction
     */
    IECData data;

    /**
     * @brief Enabled device bits
     */
    uint32_t enabledDevices;

    /**
     * @brief called in main.cpp to set up the bus.
     */
    void setup();

    /**
     * @brief Run one iteration of the bus service loop
     */
    void service();

    /**
     * @brief Send bytes to bus
     * @param buf buffer to send
     * @param len length of buffer
     */
    void sendBytes(const char *buf, size_t len);

    /**
     * @brief Send string to bus
     * @param s std::string to send
     */
    void sendBytes(std::string s);

    /**
     * @brief called in response to RESET pin being asserted.
     */
    void reset_all_our_devices();

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

    /**
     * @brief signal to bus that we timed out.
     * @return true if timed out.
     */
    bool senderTimeout();

    // true => PULL => LOW
    inline void IRAM_ATTR pull(uint8_t pin)
    {
#ifndef IEC_SPLIT_LINES
        set_pin_mode(pin, gpio_mode_t::GPIO_MODE_OUTPUT);
#endif
        fnSystem.digital_write(pin, 0);
    }

    // false => RELEASE => HIGH
    inline void IRAM_ATTR release(uint8_t pin)
    {
#ifndef IEC_SPLIT_LINES
        set_pin_mode(pin, gpio_mode_t::GPIO_MODE_OUTPUT);
#endif
        fnSystem.digital_write(pin, 1);
    }

    inline bool IRAM_ATTR status(uint8_t pin)
    {
#ifndef IEC_SPLIT_LINES
        set_pin_mode(pin, gpio_mode_t::GPIO_MODE_INPUT);
#endif
        return gpio_get_level((gpio_num_t)pin) ? 0 : 1;
    }

    inline void IRAM_ATTR set_pin_mode(uint8_t pin, gpio_mode_t mode)
    {
        static uint64_t gpio_pin_modes;
        uint8_t b_mode = (mode == 1) ? 1 : 0;

        // is this pin mode already set the way we want?
#ifndef IEC_SPLIT_LINES
        if (((gpio_pin_modes >> pin) & 1ULL) != b_mode)
#endif
        {
            // toggle bit so we don't change mode unnecessarily
            gpio_pin_modes ^= (-b_mode ^ gpio_pin_modes) & (1ULL << pin);

            gpio_config_t io_conf =
                {
                    .pin_bit_mask = (1ULL << pin),         // bit mask of the pins that you want to set
                    .mode = mode,                          // set as input mode
                    .pull_up_en = GPIO_PULLUP_DISABLE,     // disable pull-up mode
                    .pull_down_en = GPIO_PULLDOWN_DISABLE, // disable pull-down mode
                    .intr_type = GPIO_INTR_DISABLE         // interrupt of falling edge
                };
            // configure GPIO with the given settings
            gpio_config(&io_conf);
        }
    }
};

/**
 * @brief Return
 */
extern systemBus IEC;

#endif /* I2C_H */