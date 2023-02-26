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
     * @brief Message queue
     */
    QueueHandle_t qMessages = nullptr;

    /**
     * @brief Send the desired buffer to the IEC.
     * @param buff The byte buffer to send to the IEC.
     * @param len The length of the buffer to send to the IEC.
     * @return TRUE if the IEC processed the data in error, FALSE if the Iec successfully processed
     * the data.
     */
    void bus_to_computer(uint8_t *buff, uint16_t len, bool err);

    /**
     * @brief Receive data from the IEC.
     * @param buff The byte buffer provided for data from the IEC.
     * @param len The length of the amount of data to receive from the IEC.
     * @return An 8-bit wrap-around checksum calculated by the IEC, which should be checked with iec_checksum()
     */
    uint8_t bus_to_peripheral(uint8_t *buff, uint16_t len);

    /**
     * @brief Return the two aux bytes in cmdFrame as a single 16-bit value, commonly used, for example to retrieve
     * a sector number, for disk, or a number of bytes waiting for the iecNetwork device.
     *
     * @return 16-bit value of DAUX1/DAUX2 in cmdFrame.
     */
    unsigned short iec_get_aux() { return cmdFrame.aux1 | (cmdFrame.aux2 << 8); };

    /**
     * @brief All IEC commands by convention should return a status command, using bus_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void status() = 0;

    /**
     * @brief All IEC devices repeatedly call this routine to fan out to other methods for each command.
     * This is typcially implemented as a switch() statement.
     */
    virtual void process(uint32_t commanddata, uint8_t checksum) = 0;

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
     * @brief called to process the next command
     */
    void process_cmd();

    /**
     * @brief called to process a queue item (such as disk swap)
     */
    void process_queue();

public:
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
};

/**
 * @brief Return
 */
extern systemBus IEC;

#endif /* I2C_H */