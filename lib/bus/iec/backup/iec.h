#ifndef BUS_IEC_IEC_H
#define BUS_IEC_IEC_H


//#include "../../../include/global_defines.h"
#include "../../../include/cbmdefines.h"
#include "../../../include/petscii.h"
//#include "string_utils.h"

#include "protocol/cbmstandardserial.h"
//#include "protocol/jiffydos.h"

#define	IEC_CMD_MAX_LENGTH 	100

using namespace Protocol;

class IEC
{
public:
	// Return values for service:
	enum BusState
	{
		BUS_IDLE = 0,		  // Nothing recieved of our concern
		BUS_COMMAND = 1,      // A command is recieved
		BUS_LISTEN = 2,       // A command is recieved and data is coming to us
		BUS_TALK = 3,	      // A command is recieved and we must talk now
		BUS_ERROR = 5,		  // A problem occoured, reset communication
		BUS_RESET = 6		  // The IEC bus is in a reset state (RESET line).
	};

	// IEC commands:
	enum Command
	{
		IEC_GLOBAL = 0x00,	   // 0x00 + cmd (global command)
        IEC_LISTEN = 0x20,     // 0x20 + device_id (LISTEN) (0-30)
        IEC_UNLISTEN = 0x3F,   // 0x3F (UNLISTEN)
		IEC_TALK = 0x40,	   // 0x40 + device_id (TALK) (0-30)
		IEC_UNTALK = 0x5F,	   // 0x5F (UNTALK)
		IEC_SECOND = 0x60,     // 0x60 + channel (OPEN CHANNEL) (0-15)
		IEC_CLOSE = 0xE0,	   // 0xE0 + channel (CLOSE NAMED CHANNEL) (0-15)
		IEC_OPEN = 0xF0	       // 0xF0 + channel (OPEN NAMED CHANNEL) (0-15)
	};

	typedef struct _tagIECCMD
	{
		uint8_t command;
		uint8_t device;
		uint8_t channel;
		std::string content;
	} Data;

	IEC();
	~IEC() {};

	// Initialise iec driver
	bool init();

	// Checks if CBM is sending an attention message. If this is the case,
	// the message is recieved and stored in iec_data.
	BusState service(Data &iec_data);

	// Checks if CBM is sending a reset (setting the RESET line high). This is typicall
	// when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
//	bool checkRESET();

	// Sends a byte. The communication must be in the correct state: a load command
	// must just have been recieved. If something is not OK, FALSE is returned.
	bool send(uint8_t data);
	bool send(std::string data);

	// Same as IEC_send, but indicating that this is the last byte.
	bool sendEOI(uint8_t data);

	// A special send command that informs file not found condition
	bool sendFNF();

	// Recieves a byte
	int16_t receive(uint8_t device = 0);

	// Enabled Device Bit Mask
	uint32_t enabledDevices;
	bool isDeviceEnabled(const uint8_t deviceNumber);
	void enableDevice(const uint8_t deviceNumber);
	void disableDevice(const uint8_t deviceNumber);

	void debugTiming();

	uint8_t state();

	CBMStandardSerial protocol;	

private:
	// IEC Bus Commands
	BusState deviceListen(Data &iec_data);	  // 0x20 + device_id   Listen, device (0–30)
	void deviceUnListen(void);                // 0x3F               Unlisten, all devices
	BusState deviceTalk(Data &iec_data);	  // 0x40 + device_id 	Talk, device (0–30)
	void deviceUnTalk(void);                  // 0x5F               Untalk, all devices
	BusState deviceSecond(Data &iec_data);    // 0x60 + channel     Reopen, channel (0–15)
	BusState deviceClose(Data &iec_data);     // 0xE0 + channel     Close, channel (0–15)
	BusState deviceOpen(Data &iec_data);      // 0xF0 + channel     Open, channel (0–15)

	bool turnAround(void);
	bool undoTurnAround(void);
	void releaseLines(bool wait = true);

protected:

};

#endif //BUS_IEC_IEC_H
