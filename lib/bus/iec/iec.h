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

//
// https://www.pagetable.com/?p=1135
// https://pagetable.com/c64ref/c64disasm/#ED40
// http://unusedino.de/ec64/technical/misc/c1541/romlisting.html#E85B
// https://eden.mose.org.uk/gitweb/?p=rom-reverse.git;a=blob;f=src/vic-1541-sfd.asm;hb=HEAD
// https://www.pagetable.com/docs/Inside%20Commodore%20DOS.pdf
// http://www.ffd2.com/fridge/docs/1541dis.html#E853
// http://unusedino.de/ec64/technical/aay/c1541/
// http://unusedino.de/ec64/technical/aay/c1581/
// http://www.bitcity.de/1541%20Serial%20Interface.htm
// http://www.bitcity.de/theory.htm
// https://comp.sys.cbm.narkive.com/ebz1uFEx/annc-vip-the-virtual-iec-peripheral
// https://www.djupdal.org/cbm/iecata/
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/dedic_gpio.html
// https://esp32.com/viewtopic.php?t=27963
// https://github.com/alwint3r/esp32-read-multiple-gpio-pins
// https://github.com/alwint3r/esp32-set-clear-multiple-gpio
// https://www.commodore.ca/wp-content/uploads/2018/11/Commodore-IEC-Serial-Bus-Manual-C64-Plus4.txt
//

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

#include "protocol/_protocol.h"
#include "protocol/jiffydos.h"
#ifdef PARALLEL_BUS
#include "protocol/dolphindos.h"
#endif

#include <soc/gpio_reg.h>

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

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

// Return values for service:
typedef enum
{
    BUS_OFFLINE = -4,   // Bus is empty
    BUS_RESET = -3,     // The bus is in a reset state (RESET line).
    BUS_ERROR = -2,     // A problem occoured, reset communication
    BUS_RELEASE = -1,   // Clean Up
    BUS_IDLE = 0,       // Nothing recieved of our concern
    BUS_ACTIVE = 1,     // ATN is pulled and a command byte is expected
    BUS_PROCESS = 2,    // A command is ready to be processed
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
    DEVICE_IDLE = 0,    // Ready and waiting
    DEVICE_ACTIVE = 1,
    DEVICE_LISTEN = 2,  // A command is recieved and data is coming to us
    DEVICE_TALK = 3,    // A command is recieved and we must talk now
    DEVICE_PROCESS = 4, // Execute device command
} device_state_t;

typedef enum {
    PROTOCOL_SERIAL,
    PROTOCOL_FAST_SERIAL,
    PROTOCOL_SAUCEDOS,
    PROTOCOL_JIFFYDOS,
    PROTOCOL_EPYXFASTLOAD,
    PROTOCOL_WARPSPEED,
    PROTOCOL_SPEEDDOS,
    PROTOCOL_DOLPHINDOS,
    PROTOCOL_WIC64,
    PROTOCOL_IEEE488
} bus_protocol_t;

using namespace Protocol;

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
     * @brief the raw bytes received for the command
     */
    std::vector<uint8_t> payload_raw;
    /**
     * @brief secondary action description
     */
    std::string action ="";
    /**
     * @brief clear and initialize IEC command data
     */
    void init(void)
    {
        //primary = 0;
        device = 0;
        secondary = 0;
        channel = 0;
        payload = "";
        payload_raw.clear();
        action = "";
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
private:

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
     * @brief The current device command in raw PETSCII. Used when payload is converted to ASCII for Basic commands
     */
    std::string payloadRaw;

    /**
     * @brief pointer to the current command data
     */
    IECData commanddata;

    /**
     * @brief current device state.
     */
    device_state_t state;

    /**
     * @brief response queue (e.g. INPUT)
     * @deprecated remove as soon as it's out of fuji.
     */
    std::queue<std::string> response_queue;

    /**
     * @brief tokenized payload
     */
    std::vector<std::string> pt;
    std::vector<uint8_t> pti;

    /**
     * @brief The status information to send back on cmd input
     * @param error = the latest error status
     * @param msg = most recent status message
     * @param connected = is most recent channel connected?
     * @param channel = channel of most recent status msg.
     */
    struct _iecStatus
    {
        int8_t error;
        uint8_t cmd;
        std::string msg;
        bool connected;
        int channel;
    } iecStatus;

    /**
     * @brief Get device ready to handle next phase of command.
     */
    device_state_t queue_command(const IECData &data)
    {
        commanddata = data;

        if (commanddata.primary == IEC_LISTEN)
            state = DEVICE_LISTEN;
        else if (commanddata.primary == IEC_UNLISTEN)
            state = DEVICE_IDLE;
        else if (commanddata.primary == IEC_TALK)
            state = DEVICE_TALK;
        else if (commanddata.primary == IEC_UNTALK)
            state = DEVICE_IDLE;

        return state;
    }

    /**
     * @brief All IEC devices repeatedly call this routine to fan out to other methods for each command.
     *        This is typcially implemented as a switch() statement.
     * @return new device state.
     */
    virtual device_state_t process();

    /**
     * @brief poll whether interrupt should be wiggled
     * @param c secondary channel (0-15)
     */
    virtual void poll_interrupt(unsigned char c) {}

    /**
     * @brief Dump the current IEC frame to terminal.
     */
    void dumpData();

    /**
     * @brief If response queue is empty, Return 1 if ANY receive buffer has data in it, else 0
     */
    virtual void iec_talk_command_buffer_status();

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
     * The spinlock for the ESP32 hardware timers. Used for interrupt rate limiting.
     */
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

    /**
     * @brief Get the systemBus object that this virtualDevice is attached to.
     */
    systemBus get_bus();

    void set_iec_status(int8_t error, uint8_t cmd, const std::string msg, bool connected, int channel) {
        iecStatus.error = error;
        iecStatus.cmd = cmd;
        iecStatus.msg = msg;
        iecStatus.connected = connected;
        iecStatus.channel = channel;
    }

    // TODO: does this need to translate the message to PETSCII?
    std::vector<uint8_t> iec_status_to_vector() {
        std::vector<uint8_t> data;
        data.push_back(static_cast<uint8_t>(iecStatus.error));
        data.push_back(iecStatus.cmd);
        data.push_back(iecStatus.connected ? 1 : 0);
        data.push_back(static_cast<uint8_t>(iecStatus.channel & 0xFF)); // it's only an int because of atoi from some basic commands, but it's never really more than 1 byte

        // max of 41 chars in message including the null terminator. It will simply be truncated, so if we find any that are excessive, should trim them down in firmware
        size_t actualLength = std::min(iecStatus.msg.length(), static_cast<size_t>(40));
        for (size_t i = 0; i < actualLength; ++i) {
            data.push_back(static_cast<uint8_t>(iecStatus.msg[i]));
        }
        data.push_back(0); // null terminate the string

        return data;
    }

    void reset_state() {
        payload.clear();
        std::queue<std::string>().swap(response_queue);
        pt.clear();
        pt.shrink_to_fit();
    }
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
     * @brief the detected bus protocol
     */
    bus_protocol_t detected_protocol = PROTOCOL_SERIAL;  // default is IEC Serial

    /**
     * @brief the active bus protocol
     */
    std::shared_ptr<IECProtocol> protocol = nullptr;

    /**
     * @brief Switch to detected bus protocol
     */
    std::shared_ptr<IECProtocol> selectProtocol();

    /**
     * IEC LISTEN received
     */
    void deviceListen();

    /**
     * IEC TALK requested
     */
    void deviceTalk();

    /**
     * BUS TURNAROUND (switch from listener to talker)
     */
    bool turnAround();

    /**
     * @brief called to process the next command
     */
    void process_cmd();

    /**
     * @brief called to process a queue item (such as disk swap)
     */
    void process_queue();

    /**
     * @brief called to read bus command bytes
    */
    void read_command();

    /**
     * @brief called to read bus payload bytes
    */
    void read_payload();

    /**
     * ESP timer handle for the Interrupt rate limiting timer
     */
    esp_timer_handle_t rateTimerHandle = nullptr;

    /**
     * Timer Rate for interrupt timer
     */
    int timerRate = 100;

    /**
     * @brief Start the Interrupt rate limiting timer
     */
    void timer_start();

    /**
     * @brief Stop the Interrupt rate limiting timer
     */
    void timer_stop();

public:
    /**
     * @brief bus flags
     */
    uint16_t flags = CLEAR;

    /**
     * @brief bus enabled
     */
    bool enabled = true;

    /**
     * @brief current bus state
     */
    bus_state_t state;

    /**
     * @brief vic20 mode enables faster valid bit timing
     */
    bool vic20_mode = false;

    /**
     * Toggled by the rate limiting timer to indicate that the SRQ interrupt should
     * be pulsed.
     */
    bool interruptSRQ = false;    

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
     * @brief Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
     */
    void assert_interrupt();

    /**
     * @brief Release the bus lines, we're done.
     */
    void releaseLines(bool wait = false);

    /**
     * @brief Set 2bit fast loader pair timing
     * @param set Send 's', Receive 'r'
     * @param p1 Pair 1
     * @param p2 Pair 2
     * @param p3 Pair 3
     * @param p4 Pair 4
     */
    void setBitTiming(std::string set, int p1 = 0, int p2 = 0, int p3 = 0, int p4 = 0);

    /**
     * @brief send single byte
     * @param c byte to send
     * @param eoi Send EOI?
     * @return true on success, false on error
    */
    bool sendByte(const char c, bool eoi = false);

    /**
     * @brief Send bytes to bus
     * @param buf buffer to send
     * @param len length of buffer
     * @param eoi Send EOI?
     * @return true on success, false on error
     */
    bool sendBytes(const char *buf, size_t len, bool eoi = true);

    /**
     * @brief Send string to bus
     * @param s std::string to send
     * @param eoi Send EOI?
     * @return true on success, false on error
     */
    bool sendBytes(std::string s, bool eoi = true);

    /**
     * @brief Receive Byte from bus
     * @return Byte received from bus, or -1 for error
     */
    uint8_t receiveByte();

    /**
     * @brief Receive String from bus
     * @return std::string received from bus
     */
    std::string receiveBytes();

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
     * @brief Check if device is enabled
     * @param deviceNumber The device ID to check
     */
    bool isDeviceEnabled ( const uint8_t device_id );

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
     */
    void senderTimeout();


    uint8_t bit = 0;
    uint8_t byte = 0;

    bool pin_atn = false;
    bool pin_clk = false;
    bool pin_data = false;
    bool pin_srq = false;
    bool pin_reset = false;

    void init_gpio(gpio_num_t _pin);
    void pull ( uint8_t _pin );
    void release ( uint8_t _pin );
    bool status ( uint8_t _pin );
    bool status ();

    void debugTiming();
};
/**
 * @brief Return
 */
extern systemBus IEC;

#endif /* IEC_H */
