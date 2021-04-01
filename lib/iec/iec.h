#ifndef IECBUS_H
#define IECBUS_H

/**
 * notes by jeffpiep 3/9/2021
 * 
 * i think we can make an iecBus class that listens for ATN
 * gets the command and then passes off control to the iecDevice
 * IIRC, we do this in SIO land: the sioDevice is a friend to the sioBus. 
 * The sioBus has a list of devices. The bus object (SIO) listens for commands.
 * When it sees a command, it finds out what device the command is directed
 * towards and then hands over control to that device object. Most of the SIO
 * commands and operation belong to the sioDevice class (e.g., ack, nak, 
 * _to_computer, _to_device). 
 * 
 * This file, iec.h, is partly a low-level driver for the IEC physical layer. 
 * We need this because we aren't using a UART. i think we might want to
 * break out the low level i/o into a differnt class - something to think about.
 * then there's the standard IEC protocol layer with commands and data state. 
 * 
 * The devices currently exist in the interface.h file. it is a disk device
 * and a realtime clock. it would great if we could port the SD2IEC devices
 * because they support JiffyDOS, TFC3 turbo, etc. There should be a minimum
 * set of capability that we can define in the base class iecDevice using
 * virual functions. I think that is the handlers for the ATN commands
 * and probably data input and output. maybe error reporting on channel 15?
 * maybe we want a process command to deal with incoming data, e.g., on the 
 * printer? 

*/

#include "fnSystem.h"

#include "cbmdefines.h"
#include "Petscii.h"

// #define TWO_IO_PINS
#undef TWO_IO_PINS

// ESP32 GPIO to C64 IEC Serial Port
#define IEC_PIN_ATN     22      // PROC
#define IEC_PIN_SRQ     26      // INT

#ifndef TWO_IO_PINS
#define IEC_PIN_CLK     27      // CKI
#define IEC_PIN_DATA    32      // CKO
#else
#define IEC_PIN_CLK_IN  27      // CKI
#define IEC_PIN_CLK     32      // CKO
#define IEC_PIN_DATA_IN 21      // DI
#define IEC_PIN_DATA    33      // DO
#endif
//#define IEC_PIN_RESET   D8      // IO15

// IEC protocol timing consts:
#define TIMING_BIT          60  // bit clock hi/lo time     (us)
#define TIMING_NO_EOI       5   // delay before bits        (us)
#define TIMING_EOI_WAIT     200 // delay to signal EOI      (us)
#define TIMING_EOI_THRESH   20  // threshold for EOI detect (*10 us approx)
#define TIMING_STABLE_WAIT  20  // line stabilization       (us)
#define TIMING_ATN_PREDELAY 50  // delay required in atn    (us)
#define TIMING_ATN_DELAY    100 // delay required after atn (us)
#define TIMING_FNF_DELAY    100 // delay after fnf?         (us)
#define TIMING_SLOW_DOWN    50  // slow down a little       (us)
#define TIMING_EXTRA		5	

// See timeoutWait
#define TIMEOUT 65500

class iecBus
{
public:
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
	enum ATNCheck 
	{
		ATN_IDLE = 0,           // Nothing recieved of our concern
		ATN_CMD = 1,            // A command is recieved
		ATN_CMD_LISTEN = 2,     // A command is recieved and data is coming to us
		ATN_CMD_TALK = 3,       // A command is recieved and we must talk now
		ATN_ERROR = 4,          // A problem occoured, reset communication
		ATN_RESET = 5		    // The IEC bus is in a reset state (RESET line).
	};

	// IEC ATN commands:
	enum ATNCommand 
    {
		ATN_CODE_GLOBAL = 0x00,	    // 0x00 + cmd (global command)
		ATN_CODE_LISTEN = 0x20,	    // 0x20 + device_id (LISTEN)
		ATN_CODE_UNLISTEN = 0x3F,   // 0x3F (UNLISTEN)
		ATN_CODE_TALK = 0x40,	    // 0x40 + device_id (TALK)
		ATN_CODE_UNTALK = 0x5F,     // 0x5F (UNTALK)
		ATN_CODE_DATA = 0x60,	    // 0x60 + channel (SECOND)
		ATN_CODE_CLOSE = 0xE0,  	// 0xE0 + channel (CLOSE)
		ATN_CODE_OPEN = 0xF0		// 0xF0 + channel (OPEN)
	};

	// ATN command struct maximum command length:
	enum 
	{
		ATN_CMD_MAX_LENGTH = 40
	};
	
	typedef struct _tagATNCMD 
	{
		int code;
		int command;
		int channel;
		int device;
		int str[ATN_CMD_MAX_LENGTH];
		int strLen;
	} ATNCmd;

	iecBus();
	~iecBus(){}

	// Initialise iec driver
	bool init();

	// Checks if CBM is sending an attention message. If this is the case,
	// the message is recieved and stored in atn_cmd.
	ATNCheck checkATN(ATNCmd& atn_cmd);

	// Checks if CBM is sending a reset (setting the RESET line high). This is typicall
	// when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
//	bool checkRESET();

	// Sends a int. The communication must be in the correct state: a load command
	// must just have been recieved. If something is not OK, FALSE is returned.
	bool send(int data);

	// Same as IEC_send, but indicating that this is the last int.
	bool sendEOI(int data);

	// A special send command that informs file not found condition
	bool sendFNF();

	// Recieves a int
	int receive();

	// Enabled Device Bit Mask
	uint32_t enabledDevices;
	bool isDeviceEnabled(const int deviceNumber);
	void enableDevice(const int deviceNumber);
	void disableDevice(const int deviceNumber);

	IECState state() const;


private:
	// IEC Bus Commands
	ATNCheck deviceListen(ATNCmd& atn_cmd);		// 0x20 + device_id 	Listen, device (0–30)
	ATNCheck deviceUnListen(ATNCmd& atn_cmd);	// 0x3F 				Unlisten, all devices
	ATNCheck deviceTalk(ATNCmd& atn_cmd);		// 0x40 + device_id 	Talk, device 
	ATNCheck deviceUnTalk(ATNCmd& atn_cmd);		// 0x5F 				Untalk, all devices 
	ATNCheck deviceReopen(ATNCmd& atn_cmd);		// 0x60 + channel		Reopen, channel (0–15)
	ATNCheck deviceClose(ATNCmd& atn_cmd);		// 0xE0 + channel		Close, channel
	ATNCheck deviceOpen(ATNCmd& atn_cmd);		// 0xF0 + channel		Open, channel

	ATNCheck receiveCommand(ATNCmd& atn_cmd);
	
	int receiveByte(void);
	bool sendByte(int data, bool signalEOI);
	bool timeoutWait(int iecPIN, IECline lineStatus);
	bool turnAround(void);
	bool undoTurnAround(void);

	// true => PULL => DIGI_LOW
	inline void pull(int pin)
	{
		set_pin_mode(pin, gpio_mode_t::GPIO_MODE_OUTPUT);
		fnSystem.digital_write(pin, DIGI_LOW);
	}

	// false => RELEASE => DIGI_HIGH
	inline void release(int pin)
	{
		// releasing line can set to input mode, which won't drive the bus - simple way to mimic open collector
		// *** didn't seem to work in my testing ***
        //fnSystem.set_pin_mode(pin, gpio_mode_t::GPIO_MODE_INPUT);
        set_pin_mode(pin, gpio_mode_t::GPIO_MODE_OUTPUT);
		fnSystem.digital_write(pin, DIGI_HIGH);
	}

	inline IECline status(int pin)
	{
		#ifdef TWO_IO_PINS
		if (pin == IEC_PIN_CLK)
			pin = IEC_PIN_CLK_IN;
		else if (pin == IEC_PIN_DATA)
			pin = IEC_PIN_DATA_IN;
		#endif
		
		// To be able to read line we must be set to input, not driving.
		#ifndef TWO_IO_PINS
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

	// communication must be reset
	int m_state;
};

extern iecBus IEC;

#endif
