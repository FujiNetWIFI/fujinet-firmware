#ifndef IEC_H
#define IEC_H

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
 * and a realtime clock. it would great if we could port the IEC2SD devices
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

// ESP32 GPIO to C64 IEC Serial Port
#define IEC_PIN_ATN     39      // CMD
#define IEC_PIN_CLOCK   27      // CKI
#define IEC_PIN_DATA    32      // CKO
#define IEC_PIN_SRQ     26      // INT
//#define IEC_PIN_RESET   D8      // IO15

// IEC protocol timing consts:
#define TIMING_BIT          60  // bit clock hi/lo time     (us)
#define TIMING_NO_EOI       20  // delay before bits        (us)
#define TIMING_EOI_WAIT     200 // delay to signal EOI      (us)
#define TIMING_EOI_THRESH   20  // threshold for EOI detect (*10 us approx)
#define TIMING_STABLE_WAIT  20  // line stabilization       (us)
#define TIMING_ATN_PREDELAY 50  // delay required in atn    (us)
#define TIMING_ATN_DELAY    100 // delay required after atn (us)
#define TIMING_FNF_DELAY    100 // delay after fnf?         (us)

// See timeoutWait
#define TIMEOUT 65500

#define HIGH 0x1
#define LOW  0x0

class IEC
{
public:

	enum IECState {
		noFlags   = 0,
		eoiFlag   = (1 << 0),   // might be set by iec_receive
		atnFlag   = (1 << 1),   // might be set by iec_receive
		errorFlag = (1 << 2)    // If this flag is set, something went wrong and
	};

	// Return values for checkATN:
	enum ATNCheck {
		ATN_IDLE = 0,           // Nothing recieved of our concern
		ATN_CMD = 1,            // A command is recieved
		ATN_CMD_LISTEN = 2,     // A command is recieved and data is coming to us
		ATN_CMD_TALK = 3,       // A command is recieved and we must talk now
		ATN_ERROR = 4,          // A problem occoured, reset communication
		ATN_RESET = 5		    // The IEC bus is in a reset state (RESET line).
	};

	// IEC ATN commands:
	enum ATNCommand {
		ATN_CODE_GLOBAL = 0x00,		// 0x00 + cmd (global command)
		ATN_CODE_LISTEN = 0x20,		// 0x20 + device_id (LISTEN)
		ATN_CODE_UNLISTEN = 0x3F,	// 0x3F (UNLISTEN)
		ATN_CODE_TALK = 0x40,		// 0x40 + device_id (TALK)
		ATN_CODE_UNTALK = 0x5F,		// 0x5F (UNTALK)
		ATN_CODE_DATA = 0x60,		// 0x60 + channel (SECOND)
		ATN_CODE_CLOSE = 0xE0,  	// 0xE0 + channel (CLOSE)
		ATN_CODE_OPEN = 0xF0		// 0xF0 + channel (OPEN)
	};

	// ATN command struct maximum command length:
	enum {
		ATN_CMD_MAX_LENGTH = 40
	};
	
	typedef struct _tagATNCMD {
		int code;
		int command;
		int channel;
		int device;
		int str[ATN_CMD_MAX_LENGTH];
		int strLen;
	} ATNCmd;

	IEC();
//	~IEC(){}

	// Initialise iec driver
	bool init();

	// Checks if CBM is sending an attention message. If this is the case,
	// the message is recieved and stored in atn_cmd.
	ATNCheck checkATN(ATNCmd& atn_cmd);

	// Checks if CBM is sending a reset (setting the RESET line high). This is typicall
	// when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
	bool checkRESET();

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

	inline bool readATN()
	{
		return readPIN(IEC_PIN_ATN);
	}

	inline bool readCLOCK()
	{
		return readPIN(IEC_PIN_CLOCK);
	}

	inline bool readDATA()
	{
		return readPIN(IEC_PIN_DATA);
	}

	inline bool readSRQ()
	{
		return readPIN(IEC_PIN_SRQ);
	}

//	inline bool readRESET()
//	{
//		return readPIN(IEC_PIN_RESET);
//	}

private:
	// IEC Bus Commands
	ATNCheck deviceListen(ATNCmd& atn_cmd);		// 0x20 + device_id 	Listen, device (0–30)
	ATNCheck deviceUnListen(ATNCmd& atn_cmd);	// 0x3F 				Unlisten, all devices
	ATNCheck deviceTalk(ATNCmd& atn_cmd);		// 0x40 + device_id 	Talk, device 
	ATNCheck deviceUnTalk(ATNCmd& atn_cmd);		// 0x5F 				Untalk, all devices 
	ATNCheck deviceReopen(ATNCmd& atn_cmd);		// 0x60 + channel		Reopen, channel (0–15)
	ATNCheck deviceClose(ATNCmd& atn_cmd);		// 0xE0 + channel		Close, channel
	ATNCheck deviceOpen(ATNCmd& atn_cmd);		// 0xF0 + channel		Open, channel

	int timeoutWait(int waitBit, bool whileHigh);
	int receiveByte(void);
	bool sendByte(int data, bool signalEOI);
	bool turnAround(void);
	bool undoTurnAround(void);

	// false = LOW, true == HIGH
	inline bool readPIN(int pinNumber)
	{
		// To be able to read line we must be set to input, not driving.
		fnSystem.set_pin_mode(pinNumber, gpio_mode_t::GPIO_MODE_INPUT);
		return fnSystem.digital_read(pinNumber) ? true : false;
	}

	// true == PULL == HIGH, false == RELEASE == LOW
	// TO DO: is above right? I thought PULL == LOW and RELEASE == HIGH
	inline void writePIN(int pinNumber, bool state)
	{
		// TOD O: why set input/output mode same as state?
		fnSystem.set_pin_mode(pinNumber, state ? gpio_mode_t::GPIO_MODE_OUTPUT : gpio_mode_t::GPIO_MODE_INPUT);
		fnSystem.digital_write(pinNumber, state ? LOW : HIGH);
	}

	inline void writeATN(bool state)
	{
		writePIN(IEC_PIN_ATN, state);
	}

	inline void writeCLOCK(bool state)
	{
		writePIN(IEC_PIN_CLOCK, state);
	}

	inline void writeDATA(bool state)
	{
		writePIN(IEC_PIN_DATA, state);
	}

	inline void writeSRQ(bool state)
	{
		writePIN(IEC_PIN_SRQ, state);
	}

	// communication must be reset
	int m_state;
};

extern IEC iec;

#endif
