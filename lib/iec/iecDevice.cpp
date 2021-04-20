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

#include <stdarg.h>
#include <string.h>

#include "version.h"
#include "fnSystem.h"
#include "led.h"
#include "utils.h"


#include "iecBus.h"
#include "iecDevice.h"


using namespace CBM;

iecDevice::iecDevice() // (IEC &iec, FileSystem *fileSystem)
	: _iec(IEC)
{
	reset();
} // ctor


void iecDevice::reset(void)
{
	_openState = O_NOTHING;
	_queuedError = ErrIntro;
} // reset


void iecDevice::sendStatus(void)
{
	int i, readResult;
	
	std::string status("00, OK, 00, 08");
	readResult = status.length();

	Debug_printf("\r\nsendStatus: ");
	// Length does not include the CR, write all but the last one should be with EOI.
	for (i = 0; i < readResult - 2; ++i)
		_iec.send(status[i]);

	// ...and last byte in string as with EOI marker.
	_iec.sendEOI(status[i]);
} // sendStatus


void iecDevice::sendSystemInfo()
{
	Debug_printf("\r\nsendSystemInfo:\r\n");

	// Reset basic memory pointer:
	uint16_t basicPtr = PET_BASIC_START;

	// FSInfo64 fs_info;
	// m_fileSystem->info64 ( fs_info );

	// char floatBuffer[10]; // buffer
	// dtostrf(getFragmentation(), 3, 2, floatBuffer);

	// Send load address
	_iec.send(PET_BASIC_START bitand 0xff);
	_iec.send((PET_BASIC_START >> 8) bitand 0xff);
	Debug_println("");

	// Send List HEADER
	sendLine(basicPtr, 0, "\x12 %s V%s ", PRODUCT_ID, FN_VERSION_FULL);

	// CPU
	sendLine(basicPtr, 0, "SYSTEM ---");
	// std::string sdk(esp_get_idf_version());
	// util_string_toupper(sdk);
	sendLine(basicPtr, 0, "SDK VER    : %s", fnSystem.get_sdk_version());
	// TODO: sendLine(basicPtr, 0, "BOOT VER   : %08X", ESP.getBootVersion());
	// TODO: sendLine(basicPtr, 0, "BOOT MODE  : %08X", ESP.getBootMode());
	// TODO: sendLine(basicPtr, 0, "CHIP ID    : %08X", ESP.getChipId());
	sendLine(basicPtr, 0, "CPU MHZ    : %d MHZ", fnSystem.get_cpu_frequency());
	// TODO: sendLine(basicPtr, 0, "CYCLES     : %u", ESP.getCycleCount());

	// POWER
	sendLine(basicPtr, 0, "POWER ---");
	sendLine(basicPtr, 0, "VOLTAGE    : %d.%d V", fnSystem.get_sio_voltage() / 1000, fnSystem.get_sio_voltage() % 1000);

	// RAM
	sendLine(basicPtr, 0, "MEMORY ---");
	sendLine(basicPtr, 0, "PSRAM SIZE  : %5d B", fnSystem.get_psram_size());
	sendLine(basicPtr, 0, "HEAP FREE   : %5d B", fnSystem.get_free_heap_size());
	// sendLine(basicPtr, 0, "RAM >BLK   : %5d B", getLargestAvailableBlock());
	// sendLine(basicPtr, 0, "RAM FRAG   : %s %%", floatBuffer);

	// ROM
	// sendLine(basicPtr, 0, "ROM SIZE   : %5d B", ESP.getSketchSize() + ESP.getFreeSketchSpace());
	// sendLine(basicPtr, 0, "ROM USED   : %5d B", ESP.getSketchSize());
	// sendLine(basicPtr, 0, "ROM FREE   : %5d B", ESP.getFreeSketchSpace());

	// FLASH
	// sendLine(basicPtr, 0, "STORAGE ---");
	// sendLine(basicPtr, 0, "FLASH SIZE : %5d B", ESP.getFlashChipRealSize());
	// sendLine(basicPtr, 0, "FLASH SPEED: %d MHZ", ( ESP.getFlashChipSpeed() / 1000000 ));

	// FILE SYSTEM
	// sendLine(basicPtr, 0, "FILE SYSTEM ---");
	// sendLine(basicPtr, 0, "TYPE       : %s", FS_TYPE);
	// sendLine(basicPtr, 0, "SIZE       : %5d B", fs_info.totalBytes);
	// sendLine(basicPtr, 0, "USED       : %5d B", fs_info.usedBytes);
	// sendLine(basicPtr, 0, "FREE       : %5d B", fs_info.totalBytes - fs_info.usedBytes);

	// NETWORK
	sendLine(basicPtr, 0, "NETWORK ---");
	// char ip[16];
	// sprintf ( ip, "%s", ipToString ( WiFi.softAPIP() ).c_str() );
	std::string hn(fnSystem.Net.get_hostname());
	util_string_toupper(hn);
	sendLine(basicPtr, 0, "HOSTNAME : %s", hn.c_str());
	sendLine(basicPtr, 0, "IP       : %s", fnSystem.Net.get_ip4_address_str().c_str());
	sendLine(basicPtr, 0, "GATEWAY  : %s", fnSystem.Net.get_ip4_gateway_str().c_str());
	sendLine(basicPtr, 0, "DNS      : %s", fnSystem.Net.get_ip4_dns_str().c_str());

	// End program with two zeros after last line. Last zero goes out as EOI.
	_iec.send(0);
	_iec.sendEOI(0);

	fnLedManager.set(LED_BUS);
} // sendSystemInfo

void iecDevice::sendDeviceStatus()
{
	Debug_printf("\r\nsendDeviceStatus:\r\n");

	// Reset basic memory pointer:
	uint16_t basicPtr = PET_BASIC_START;

	// Send load address
	_iec.send(PET_BASIC_START bitand 0xff);
	_iec.send((PET_BASIC_START >> 8) bitand 0xff);
	Debug_println("");

	// Send List HEADER
	sendLine(basicPtr, 0, "\x12 %s V%s ", PRODUCT_ID, FN_VERSION_FULL);

	// Current Config
	sendLine(basicPtr, 0, "DEVICE    : %d", IEC.ATN.device_id);

	// End program with two zeros after last line. Last zero goes out as EOI.
	_iec.send(0);
	_iec.sendEOI(0);

	fnLedManager.set(LED_BUS);
} // sendDeviceStatus


void iecDevice::_process(void)
{

	switch (IEC.ATN.command)
	{
		case ATN_COMMAND_OPEN:
			if ( IEC.ATN.channel == READ_CHANNEL )
			{
				Debug_printf("\r\niecDevice::service: [OPEN] LOAD \"%s\",%d ", IEC.ATN.data, IEC.ATN.device_id);
			}
			if ( IEC.ATN.channel == WRITE_CHANNEL )
			{
				Debug_printf("\r\niecDevice::service: [OPEN] SAVE \"%s\",%d ", IEC.ATN.data, IEC.ATN.device_id);	
			}

			// Open either file or prg for reading, writing or single line command on the command channel.
			// In any case we just issue an 'OPEN' to the host and let it process.
			// Note: Some of the host response handling is done LATER, since we will get a TALK or LISTEN after this.
			// Also, simply issuing the request to the host and not waiting for any response here makes us more
			// responsive to the CBM here, when the DATA with TALK or LISTEN comes in the next sequence.
			_open();
			break;

		case ATN_COMMAND_DATA:  // data channel opened
			Debug_printf("\r\niecDevice::service: [DATA] ");
			if(IEC.ATN.mode == ATN_TALK) 
			{
				// when the CMD channel is read (status), we first need to issue the host request. The data channel is opened directly.
				if(IEC.ATN.channel == CMD_CHANNEL)
				{
					_open(); // This is typically an empty command,	
				}
				
				_talk_data(IEC.ATN.channel); // Process TALK command
			}
			else if(IEC.ATN.mode == ATN_LISTEN)
			{
				_listen_data(); // Process LISTEN command
			}
			else if(IEC.ATN.mode == ATN_CMD) // Here we are sending a command to PC and executing it, but not sending response
			{
				_open(); // back to CBM, the result code of the command is however buffered on the PC side.
			}
			break;

		case ATN_COMMAND_CLOSE:
			Debug_printf("\r\niecDevice::service: [CLOSE] ");
			_close();
			break;

		case ATN_COMMAND_LISTEN:
			Debug_printf("\r\niecDevice::service:[LISTEN] ");
			break;

		case ATN_COMMAND_TALK:
			Debug_printf("\r\niecDevice::service:[TALK] ");
			break;

		case ATN_COMMAND_UNLISTEN:
			Debug_printf("\r\niecDevice::service:[UNLISTEN] ");
			break;

		case ATN_COMMAND_UNTALK:
			Debug_printf("\r\niecDevice::service:[UNTALK] ");
			break;

	} // switch

} // handler


// send single basic line, including heading basic pointer and terminating zero.
uint16_t iecDevice::sendLine(uint16_t &basicPtr, uint16_t blocks, const char* format, ...)
{
	// Format our string
	va_list args;
	va_start(args, format);
	char text[vsnprintf(NULL, 0, format, args) + 1];
	vsnprintf(text, sizeof text, format, args);
	va_end(args);

	return sendLine(basicPtr, blocks, text);
}

uint16_t iecDevice::sendLine(uint16_t &basicPtr, uint16_t blocks, char* text)
{
	int i;
	uint16_t b_cnt = 0;

	Debug_printf("%d %s ", blocks, text);

	// Get text length
	int len = strlen(text);

	// Increment next line pointer
	basicPtr += len + 5;

	// Send that pointer
	_iec.send(basicPtr bitand 0xFF);
	_iec.send(basicPtr >> 8);

	// Send blocks
	_iec.send(blocks bitand 0xFF);
	_iec.send(blocks >> 8);

	// Send line contents
	for (i = 0; i < len; i++)
		_iec.send(text[i]);

	// Finish line
	_iec.send(0);

	Debug_println("");

	b_cnt += (len + 5);

	return b_cnt;
} // sendLine


uint16_t iecDevice::sendHeader(uint16_t &basicPtr)
{
	uint16_t byte_count = 0;

	// Send List HEADER
	// "      MEAT LOAF 64      "
	//	int space_cnt = (16 - strlen(PRODUCT_ID)) / 2;
	int space_cnt = 0; //(16 - strlen(FN_VERSION_FULL)) / 2;
	byte_count += sendLine(basicPtr, 0, "\x12\"%*s%s%*s\" %.02d 2A", space_cnt, "", PRODUCT_ID, space_cnt, "", _device_id);

	return byte_count;
}


/*
   IEC WRITE to CBM from DEVICE
   buf = buffer to send from fujinet
   len = length of buffer
   err = along with data, send ERROR status to CBM rather than COMPLETE
*/
void iecDevice::iec_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    // Write data frame to computer
    Debug_printf("->IEC write %hu bytes\n", len);
#ifdef VERBOSE_IEC
    Debug_printf("SEND <%u> BYTES\n\t", len);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

	err = IEC.send(buf, len);
}

/*
   IEC READ from CBM by DEVICE
   buf = buffer from cbm to fujinet
   len = length
   Returns checksum
*/
uint8_t iecDevice::iec_to_peripheral(uint8_t *buf, uint16_t len)
{
    // Retrieve data frame from computer
    Debug_printf("<-IEC read %hu bytes\n", len);

    __BEGIN_IGNORE_UNUSEDVARS
    uint16_t l = IEC.receive(buf, len);
    __END_IGNORE_UNUSEDVARS

#ifdef VERBOSE_IEC
    Debug_printf("RECV <%u> BYTES\n\t", l);
    for (int i = 0; i < len; i++)
        Debug_printf("%02x ", buf[i]);
    Debug_print("\n");
#endif

    return;
}

// Calculate 8-bit checksum
uint8_t iec_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

// IEC COMPLETE
void iecDevice::iec_complete()
{
    fnSystem.delay_microseconds(DELAY_T5);
    Debug_println("COMPLETE!");
}

// IEC ERROR
void iecDevice::iec_error()
{
    fnSystem.delay_microseconds(DELAY_T5);
    Debug_println("ERROR!");
}