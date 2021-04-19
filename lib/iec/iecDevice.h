// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// This file is part of Meatloaf but adapted for use in the FujiNet project
// https://github.com/FujiNetWIFI/fujinet-platformio
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

#ifndef IEC_DEVICE_H
#define IEC_DEVICE_H

#include <string>
#include "esp_vfs.h"

#include "iecBus.h"
#include "fuji.h"

#include "cbmdefines.h"

#define PRODUCT_ID "FUJINET/MEATLOAF"

// The base pointer of basic.
#define PET_BASIC_START 0x0401

enum OpenState 
{
	O_NOTHING,			// Nothing to send / File not found error
	O_INFO,				// User issued a reload sd card
	O_FILE,				// A program file is opened
	O_DIR,				// A listing is requested
	O_FILE_ERR,			// Incorrect file format opened
	O_SAVE_REPLACE,		// Save-with-replace is requested
	O_SYSTEM_INFO,
	O_DEVICE_STATUS
};

class iecBus;      // declare early so can be friend

class iecDevice
{
protected:
	friend iecBus;

    int _device_id;

	void sendStatus(void);
	void sendSystemInfo(void);
	void sendDeviceStatus(void);

	uint16_t sendHeader(uint16_t &basicPtr);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, char* text);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...);

	// handler helpers.
	void _open(void);
	void _listen_data(void);
	void _talk_data(int chan);
	void _close(void);

	// our iec low level driver:
	iecBus& _iec;

	// This var is set after an open command and determines what to send next
	int _openState; // see OpenState
	int _queuedError;

    /**
     * @brief All SIO devices repeatedly call this routine to fan out to other methods for each command. 
     * This is typcially implemented as a switch() statement.
     */
    virtual void iec_process(void);

	// Reset device
	virtual void reset();

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(){};

public:
    /**
     * @brief get the SIO device Number (1-255)
     * @return The device number registered for this device
     */
    int device_id() { return _device_id; };

    /**
     * @brief Is this sioDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    /**
     * @brief is device active (turned on?)
     */
    bool device_active = true;

	iecDevice();
	virtual ~iecDevice() {}
};

#endif
