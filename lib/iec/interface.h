#ifndef INTERFACE_H
#define INTERFACE_H

#include "iec.h"
#include "device_db.h"

#include <string>

#include "cbmdefines.h"
#include "Petscii.h"
#include "../FileSystem/fnFS.h"

enum OpenState {
	O_NOTHING,			// Nothing to send / File not found error
	O_INFO,				// User issued a reload sd card
	O_FILE,				// A program file is opened
	O_DIR,				// A listing is requested
	O_FILE_ERR,			// Incorrect file format opened
	O_SAVE_REPLACE,		// Save-with-replace is requested
	O_DEVICE_INFO,
	O_DEVICE_STATUS
};

// The base pointer of basic.
#define C64_BASIC_START 0x0801

class Interface
{
public:
	Interface(IEC &iec, FileSystem *fileSystem);
	virtual ~Interface() {}

	bool begin();

	// The handler returns the current IEC state, see the iec.hpp for possible states.
	int loop(void);

	// Keeping the system date and time as set on a specific moment. The millis() will then keep the elapsed time since
	// moment the time was set.
	void setDateTime(uint16_t year, int month, int day, int hour, int minute, int second);

	// retrieve the date and time as strings. Current time will be updated according to the elapsed millis before formatting.
	// String will be of format "yyyymmdd hhmmss", if timeOnly is true only the time part will be returned as
	// "hhmmss", this fits the TIME$ variable of cbm basic 2.0 and later.
	char* dateTimeString(char* dest, bool timeOnly);

private:
	void reset(void);

	void sendStatus(void);
	void sendDeviceInfo(void);
	void sendDeviceStatus(void);

	void sendListing(void);
	void sendListingHTTP(void);
	uint16_t sendHeader(uint16_t &basicPtr);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, char* text);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...);
	uint16_t sendFooter(uint16_t &basicPtr);
	void sendFile(void);
	void sendFileHTTP(void);

	void saveFile(void);

	// handler helpers.
	void handleATNCmdCodeOpen(IEC::ATNCmd &cmd);
	void handleATNCmdCodeDataListen(void);
	void handleATNCmdCodeDataTalk(int chan);
	void handleATNCmdClose(void);

	void handleDeviceCommand(IEC::ATNCmd &cmd);
	void handleMeatLoafCommand(IEC::ATNCmd &cmd);


	// our iec low level driver:
	IEC& m_iec;

	// This var is set after an open command and determines what to send next
	int m_openState;			// see OpenState
	int m_queuedError;



	// atn command buffer struct
	IEC::ATNCmd& m_atn_cmd;

	FileSystem *m_fileSystem;
	// StaticJsonDocument<256> m_jsonHTTP;
	std::string m_lineBuffer;
	//DynamicJsonDocument m_jsonHTTPBuffer;

	DeviceDB m_device;
	std::string m_filename;
	std::string m_filetype;
};

#endif
