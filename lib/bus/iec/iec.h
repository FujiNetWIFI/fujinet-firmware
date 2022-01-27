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

#ifndef IECBUS_H
#define IECBUS_H

#include <forward_list>

#include "../../../include/pinmap.h"
#include "../../../include/cbmdefines.h"

#include "fnSystem.h"
#include "../sio/sio.h" // cmdframe_t

#define PRODUCT_ID "FUJINET/MEATLOAF"

// The base pointer of basic.
#define PET_BASIC_START     0x0401

#define	ATN_CMD_MAX_LENGTH 	40

// IEC protocol timing consts:
#define TIMING_BIT          75  // bit clock hi/lo time     (us)
#define TIMING_NO_EOI       5   // delay before bits        (us)
#define TIMING_EOI_WAIT     200 // delay to signal EOI      (us)
#define TIMING_EOI_THRESH   20  // threshold for EOI detect (*10 us approx)
#define TIMING_STABLE_WAIT  20  // line stabilization       (us)
#define TIMING_ATN_PREDELAY 50  // delay required in atn    (us)
#define TIMING_ATN_DELAY    100 // delay required after atn (us)
#define TIMING_FNF_DELAY    100 // delay after fnf?         (us)

// See timeoutWait
#define TIMEOUT 65500

#define DEVICEID_PRINTER 			0x04 // 4
#define DEVICEID_PRINTER_LAST 		0x07 // 7

#define DEVICEID_DISK 				0x08 // 8
#define DEVICEID_DISK_LAST 			0x13 // 19

#define DEVICEID_RS232 				0x14 // 20
#define DEVICEID_RS232_LAST			0x18 // 24

#define DEVICEID_FN_NETWORK 		0x19 // 25
#define DEVICEID_FN_NETWORK_LAST 	0x1B // 29

#define DEVICEID_FUJINET 			0x1E // 30

enum IECline
{
	pulled = true,
	released = false
};

enum IECState 
{
	noFlags   = 0,
	eoiFlag   = (1 << 0),   // might be set by iec_receive
	atnFlag   = (1 << 1),   // might be set by iec_receive
	errorFlag = (1 << 2)    // If this flag is set, something went wrong and
};

// Return values for checkATN:
enum ATNMode 
{
	ATN_IDLE = 0,           // Nothing recieved of our concern
	ATN_CMD = 1,            // A command is recieved
	ATN_LISTEN = 2,         // A command is recieved and data is coming to us
	ATN_TALK = 3,           // A command is recieved and we must talk now
	ATN_ERROR = 4,          // A problem occoured, reset communication
	ATN_RESET = 5		    // The IEC bus is in a reset state (RESET line).
};

// IEC ATN commands:
enum ATNCommand 
{
	ATN_COMMAND_GLOBAL = 0x00,     // 0x00 + cmd (global command)
	ATN_COMMAND_LISTEN = 0x20,     // 0x20 + device_id (LISTEN)
	ATN_COMMAND_UNLISTEN = 0x3F,   // 0x3F (UNLISTEN)
	ATN_COMMAND_TALK = 0x40,       // 0x40 + device_id (TALK)
	ATN_COMMAND_UNTALK = 0x5F,     // 0x5F (UNTALK)
	ATN_COMMAND_DATA = 0x60,       // 0x60 + channel (SECOND)
	ATN_COMMAND_CLOSE = 0xE0,  	   // 0xE0 + channel (CLOSE)
	ATN_COMMAND_OPEN = 0xF0	       // 0xF0 + channel (OPEN)
};

struct ATNData
{
	ATNMode mode;
	int code;
	int command;
	int channel;
	int device_id;
	char data[ATN_CMD_MAX_LENGTH];
};


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

// class def'ns
class iecBus;      // declare early so can be friend
class iecFuji;     // declare here so can reference it, but define in iecFuji.h
class iecPrinter;  // Printer device

class iecDevice
{
protected:
	friend iecBus;

    /**
     * @brief Device Number: 4-30
     */
    uint8_t _devnum;


    /**
     * @brief Send the desired buffer to the Atari.
     * @param buff The byte buffer to send to the Atari
     * @param len The length of the buffer to send to the Atari.
     * @return TRUE if the Atari processed the data in error, FALSE if the Atari successfully processed
     * the data.
     */
    void bus_to_computer(uint8_t *buff, uint16_t len, bool err);

    /**
     * @brief Receive data from the Atari.
     * @param buff The byte buffer provided for data from the Atari.
     * @param len The length of the amount of data to receive from the Atari.
     * @return An 8-bit wrap-around checksum calculated by the Atari, which should be checked with iec_checksum()
     */
    uint8_t bus_to_peripheral(uint8_t *buff, uint16_t len);

    /**
     * @brief Send an acknowledgement byte to the Atari 'A'
     * This should be used if the command received by the SIO device is valid, and is used to signal to the
     * Atari that we are now processing the command.
     */
    void iec_ack();

    /**
     * @brief Send a non-acknowledgement (NAK) to the Atari 'N'
     * This should be used if the command received by the SIO device is invalid, in the first place. It is not
     * the same as iec_error().
     */
    void iec_nak();

    /**
     * @brief Send a COMPLETE to the Atari 'C'
     * This should be used after processing of the command to indicate that we've successfully finished. Failure to send
     * either a COMPLETE or ERROR will result in a SIO TIMEOUT (138) to be reported in DSTATS.
     */
    void iec_complete();

    /**
     * @brief Send an ERROR to the Atari 'E'
     * This should be used during or after processing of the command to indicate that an error resulted
     * from processing the command, and that the Atari should probably re-try the command. Failure to
     * send an ERROR or COMPLTE will result in a SIO TIMEOUT (138) to be reported in DSTATS.
     */
    void iec_error();

    /**
     * @brief Return the two aux bytes in cmdFrame as a single 16-bit value, commonly used, for example to retrieve
     * a sector number, for disk, or a number of bytes waiting for the sioNetwork device.
     * 
     * @return 16-bit value of DAUX1/DAUX2 in cmdFrame.
     */
    unsigned short iec_get_aux();

    /**
     * @brief All SIO commands by convention should return a status command, using bus_to_computer() to return
     * four bytes of status information to be put into DVSTAT ($02EA)
     */
    virtual void iec_status() = 0;



    /**
     * @brief process the next packet with the active device.
     * @param b first byte of packet.
     */
    virtual void iec_process(uint8_t b);

	// Reset device
	virtual void reset(void);

    // Optional shutdown/reboot cleanup routine
    virtual void shutdown(void) {}

	// our iec low level driver:
//	iecBus& _iec;

	// This var is set after an open command and determines what to send next
	int _openState; // see OpenState
	int _queuedError;

	void sendStatus(void);
	void sendSystemInfo(void);
	void sendDeviceStatus(void);

	uint16_t sendHeader(uint16_t &basicPtr);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, char* text);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...);

	// handler helpers.
	void _open(void) {};
	void _listen_data(void) {};
	void _talk_data(int chan) {};
	void _close(void) {};

public:
    /**
     * @brief Is this iecDevice holding the virtual disk drive used to boot CONFIG?
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
    uint8_t id() { return _devnum; }

    /**
     * @brief command frame, used by network protocol, ultimately
     */
    cmdFrame_t cmdFrame;

	iecDevice(void);
	virtual ~iecDevice(void) {}
};

enum bus_message : uint16_t
{
    BUSMSG_DISKSWAP,  // Rotate disk
    BUSMSG_DEBUG_TAPE // Tape debug msg
};

struct bus_message_t
{
    bus_message message_id;
    uint16_t message_arg;
};

// typedef iec_message_t iec_message_t;

class iecBus
{
private:
	std::forward_list<iecDevice *> _daisyChain;

    int _command_frame_counter = 0;

    iecDevice *_activeDev = nullptr;
    iecFuji *_fujiDev = nullptr;
    iecPrinter *_printerdev = nullptr;

    void _bus_process_cmd(void);
    void _bus_process_queue(void);

	int _iec_state;

	// IEC Bus Commands
//	void global(void) {};            // 0x00 + cmd          Global command to all devices, Not supported on CBM
	void listen(void);               // 0x20 + device_id 	Listen, device (0–30), Devices 0-3 are reserved
	void unlisten(void) {};          // 0x3F				Unlisten, all devices
	void talk(void);                 // 0x40 + device_id 	Talk, device (0-30)
	void untalk(void) {};            // 0x5F				Untalk, all devices 
	void data(void) {};              // 0x60 + channel		Open Channel/Data, Secondary Address / Channel (0–15)
	void close(void) {};             // 0xE0 + channel		Close, Secondary Address / Channel (0–15)
	void open(void) {};              // 0xF0 + channel		Open, Secondary Address / Channel (0–15)

	void receiveCommand(void);
	
	int receiveByte(void);
	bool sendByte(int data, bool signalEOI);
	bool turnAround(void);
	bool undoTurnAround(void);
	bool timeoutWait(int iecPIN, IECline lineStatus);	

	// true => PULL => DIGI_LOW
	inline void pull(int pin)
	{
		set_pin_mode(pin, gpio_mode_t::GPIO_MODE_OUTPUT);
		fnSystem.digital_write(pin, DIGI_LOW);
	}

	// false => RELEASE => DIGI_HIGH
	inline void release(int pin)
	{
		set_pin_mode(pin, gpio_mode_t::GPIO_MODE_OUTPUT);
		fnSystem.digital_write(pin, DIGI_HIGH);
	}

	inline IECline status(int pin)
	{
		#ifndef SPLIT_LINES
			// To be able to read line we must be set to input, not driving.
			set_pin_mode(pin, gpio_mode_t::GPIO_MODE_INPUT);
		#endif

		return fnSystem.digital_read(pin) ? released : pulled;
	}

	inline int get_bit(int pin)
       {
		return fnSystem.digital_read(pin);
	}

	inline void set_bit(int pin, int bit)
	{
		return fnSystem.digital_write(pin, bit);
	}

	inline void set_pin_mode(int pin, gpio_mode_t mode)
	{
		static uint64_t gpio_pin_modes;
		int b_mode = (mode == 1) ? 1 : 0;

		// is this pin mode already set the way we want?
		if ( ((gpio_pin_modes >> pin) & 1ULL) != b_mode )
		{
			// toggle bit so we don't change mode unnecessarily 
			gpio_pin_modes ^= (-b_mode ^ gpio_pin_modes) & (1ULL << pin);

			gpio_config_t io_conf;

			// disable interrupt
			io_conf.intr_type = GPIO_INTR_DISABLE;

			// set mode
			io_conf.mode = mode;

			io_conf.pull_up_en = gpio_pullup_t::GPIO_PULLUP_DISABLE;
			io_conf.pull_down_en = gpio_pulldown_t::GPIO_PULLDOWN_DISABLE;

			// bit mask of the pin to set
			io_conf.pin_bit_mask = 1ULL << pin;

			// configure GPIO with the given settings
			gpio_config(&io_conf);
		}
	}


public:
    void setup();
    void service();
    void shutdown();
    void reset();

    /**
     * @brief Wait for AdamNet bus to become idle.
     */
    void wait_for_idle();

    /**
     * stopwatch
     */
    int64_t start_time;

    int numDevices();
    void addDevice(iecDevice *pDevice, uint8_t device_id);
    void remDevice(iecDevice *pDevice);
    void remDevice(uint8_t device_id);
    bool deviceExists(uint8_t device_id);
    void enableDevice(uint8_t device_id);
    void disableDevice(uint8_t device_id);
    iecDevice *deviceById(uint8_t device_id);
    void changeDeviceId(iecDevice *pDevice, uint8_t device_id);

	iecPrinter *getPrinter(void) { return _printerdev; }

	QueueHandle_t qBusMessages = nullptr;

	ATNData ATN;

	// Sends a byte. The communication must be in the correct state: a load command
	// must just have been recieved. If something is not OK, FALSE is returned.
	bool send(uint8_t data);

	// Sends a string.
	bool send(uint8_t *data, uint16_t len);

	// Same as IEC_send, but indicating that this is the last byte.
	bool sendEOI(uint8_t data);

	// A special send command that informs file not found condition
	bool sendFNF(void);

	// Recieve a byte
	int receive(void);

	// Receive a string.
	bool receive(uint8_t *data, uint16_t len);

	// Enabled Device Bit Mask
	uint32_t enabledDevices;
	bool isDeviceEnabled(const int deviceNumber);
	void enableDevice(const int deviceNumber);
	void disableDevice(const int deviceNumber);

	IECState state(void) const;
};

extern iecBus IEC;

#endif // IECBUS_H
