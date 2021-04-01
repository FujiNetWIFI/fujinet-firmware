#ifndef IECDEVICE_H
#define IECDEVICE_H

#include "device_db.h"
#include "iec.h"

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

#define IMAGE_TYPES "D64|D71|D80|D81|D82|D8B|G64|X64|Z64|TAP|T64|TCRT|CRT|D1M|D2M|D4M|DHD|HDD|DNP|DFI|M2I|NIB"
#define FILE_TYPES "C64|PRG|P00|SEQ|S00|USR|U00|REL|R00"

#define TEST_PRG 0x08 0x01 0x44 0x4f 0x4c 0x50 0x58 0x20 0x22 0x3b 0x3a 0x89 0x31 0x30 0x00 0x00 0x00
class iecDevice
{
public:
	iecDevice();// iecBus &iec, FileSystem *fileSystem);
	virtual ~iecDevice() {}

	bool begin(iecBus &iec, FileSystem *fileSystem);

	// The handler returns the current IEC state, see the iec.hpp for possible states.
	int loop(void);

private:
	void reset(void);

	void sendStatus(void);
	void sendDeviceInfo(void);
	void sendDeviceStatus(void);

	void sendListing(void);
	// void sendListingHTTP(void);
	uint16_t sendHeader(uint16_t &basicPtr);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, char* text);
	uint16_t sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...);
	uint16_t sendFooter(uint16_t &basicPtr);
	void sendFile(void);
	// void sendFileHTTP(void);

	void saveFile(void);

	// handler helpers.
	void handleATNCmdCodeOpen(iecBus::ATNCmd &cmd);
	void handleATNCmdCodeDataListen(void);
	void handleATNCmdCodeDataTalk(int chan);
	void handleATNCmdClose(void);

	void handleDeviceCommand(iecBus::ATNCmd &cmd);
	void handleMeatLoafCommand(iecBus::ATNCmd &cmd);

	// our iec low level driver:
	iecBus& m_iec;

	// This var is set after an open command and determines what to send next
	int m_openState;			// see OpenState
	int m_queuedError;

	// atn command buffer struct
	iecBus::ATNCmd& m_atn_cmd;

	FileSystem *m_fileSystem;
	// StaticJsonDocument<256> m_jsonHTTP;
	std::string m_lineBuffer;
	//DynamicJsonDocument m_jsonHTTPBuffer;

	DeviceDB m_device;
	std::string m_filename;
	std::string m_filetype;
};

#endif
