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

#ifdef BUILD_CBM

#include "bus.h"

#include <stdarg.h>
#include <string.h>

#include "../../../include/version.h"
#include "../../../include/debug.h"
#include "../../../include/cbmdefines.h"

#include "led.h"
#include "fnSystem.h"
#include "fnConfig.h"
#include "utils.h"

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

    return 0;
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
    Debug_println("COMPLETE!");
}

// IEC ERROR
void iecDevice::iec_error()
{
    Debug_println("ERROR!");
}



// Read and process a command frame from SIO
void iecBus::_iec_process_cmd(void)
{
    // if (_modemDev != nullptr && _modemDev->modemActive)
    // {
    //     _modemDev->modemActive = false;
    //     Debug_println("Modem was active - resetting SIO baud");
    //     fnUartSIO.set_baudrate(_sioBaud);
    // }


    // Turn on the BUS indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

	// find device and pass control
	for (auto devicep : _daisyChain)
	{
		if (ATN.device_id == devicep->_device_id)
		{
			_activeDev = devicep;
			// handle command
			_activeDev->_process();
		}
	}

    fnLedManager.set(eLed::LED_BUS, false);
}


// IEC Bus Commands
void iecBus::listen(void)
{
	// Okay, we will listen.
	Debug_printf("(20 LISTEN) (%.2d DEVICE)", ATN.device_id);


	if (ATN.command == ATN_COMMAND_DATA) // 0x60 OPEN CHANNEL / DATA + Secondary Address / channel (0-15)
	{
		// If this 
		if (ATN.channel == CMD_CHANNEL)
		{
			receiveCommand();
			return;
		}
		else
		{
			// A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
			Debug_printf("\r\nlisten: %.2X (DATA) (%.2X COMMAND) (%.2X CHANNEL)", ATN.code, ATN.command, ATN.channel);
			ATN.mode = ATN_LISTEN;
			return;
		}
	}
	else if (ATN.command == ATN_COMMAND_OPEN) // 0xF0 OPEN CHANNEL / DATA + Secondary Address / channel (0-15)
	{
		Debug_printf("\r\nlisten: %.2X (%.2X OPEN) (%.2X CHANNEL)", ATN.code, ATN.command, ATN.channel);
		receiveCommand();
		return;
	}	
	else
	{
		if (ATN.command == ATN_COMMAND_CLOSE) // 0xE0 CLOSE CHANNEL / DATA + Secondary Address / channel (0-15)
		{
			Debug_printf("\r\nlisten: %.2X (%.2X CLOSE) (%.2X CHANNEL)", ATN.code, ATN.command, ATN.channel);
		}		
	}

	ATN.mode = ATN_IDLE;
	return;
}

void iecBus::talk(void)
{
	int i = 0;
	int c;

	// Okay, we will talk soon
	Debug_printf("(40 TALK) (%.2d DEVICE)", ATN.device_id);
	Debug_printf("\r\ntalk: %.2X (%.2X SECOND) (%.2X CHANNEL)", ATN.code, ATN.command, ATN.channel);

	while(status(IEC_PIN_ATN) == pulled) 
	{
		if(status(IEC_PIN_CLK) == released) 
		{
			c = receive();
			if (_iec_state bitand errorFlag)
			{
				ATN.mode = ATN_ERROR;
				return;
			}

			if (i >= ATN_CMD_MAX_LENGTH)
			{
				// Buffer is going to overflow, this is an error condition
				// FIXME: here we should propagate the error type being overflow so that reading error channel can give right code out.
				ATN.mode = ATN_ERROR;
				return;
			}
			ATN.data[i++] = c;
			ATN.data[i] = '\0';
		}
	}

	// Now ATN has just been released, do bus turnaround
	if (not turnAround())
	{
		ATN.mode = ATN_ERROR;
		return;
	}

	// We have recieved a CMD and we should talk now:
	ATN.mode = ATN_TALK;
	return;
}

void iecBus::receiveCommand(void)
{
	int i = 0;
	int c;

	// Some other command. Record the cmd string until UNLISTEN is sent
	while(status(IEC_PIN_ATN) == released)
	{
		// Let's get the command!
		c = receive();

		if (_iec_state bitand errorFlag)
		{
			Debug_printf("\r\nreceiveCommand: receiving LISTEN command");
			ATN.mode = ATN_ERROR;
			return;
		}

		if (i >= ATN_CMD_MAX_LENGTH)
		{
			// Buffer is going to overflow, this is an error condition
			// FIXME: here we should propagate the error type being overflow so that reading error channel can give right code out.
			Debug_printf("\r\nreceiveCommand: ATN_CMD_MAX_LENGTH");
			ATN.mode = ATN_ERROR;
			return;
		}

		ATN.data[i++] = c;
		ATN.data[i] = '\0';

		// Is this the end of the command? Was EOI sent?
		if (_iec_state bitand eoiFlag)
		{
			Debug_printf("\r\nreceiveCommand: [%s] + EOI", ATN.data);
			ATN.mode = ATN_CMD;
			return;
		}		
	}

	ATN.mode = ATN_IDLE;
	return;
}


// (Jim Butterfield - Compute! July 1983 - "HOW THE VIC/64 SERIAL BUS WORKS")
// STEP 1: READY TO SEND (We are listener now)
// Sooner or later, the talker will want to talk, and send a character. 
// When it's ready to go, it releases the Clock line to false.  This signal change might be 
// translated as "I'm ready to send a character." The listener must detect this and respond, 
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's  
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's  
// a printer chugging out a line of print, or a disk drive with a formatting job in progress, 
// it might holdback for quite a while; there's no time limit. 
int iecBus::receiveByte(void)
{
	_iec_state = noFlags;

	// Sample ATN and set flag to indicate SELECT or DATA mode
	if(status(IEC_PIN_ATN) == pulled)
		_iec_state or_eq atnFlag;

	// Talker ready to send
	if (timeoutWait(IEC_PIN_CLK, released))
	{
		Debug_println("receiveByte: talker ready to send");
		return -1;
	}

	// STEP 2: READY FOR DATA (We are listener now)
	// When  the  listener  is  ready  to  listen,  it  releases  the  Data  
	// line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false 
	// only when all listeners have released it - in other words, when  all  listeners  are  ready  
	// to  accept  data.  What  happens  next  is  variable.     
	release(IEC_PIN_DATA);

	// Wait for other listeners to be ready
	if (timeoutWait(IEC_PIN_DATA, released))
	{
		Debug_println("receiveByte: wait for all listeners to be ready");
		return -1;
	}

	// Either  the  talker  will pull the 
	// Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it  
	// will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass  
	// without  the Clock line going to true, it has a special task to perform: note EOI.
	int n = 0;
	while ((status(IEC_PIN_CLK) == released) && (n < 20))
	{
		fnSystem.delay_microseconds(10); // this loop should cycle in about 10 us...
		n++;
	}

	if (n >= TIMING_EOI_THRESH)
	{

		// INTERMISSION: EOI (We are listener now)
		// If the Ready for Data signal isn't acknowledged by the talker within 200 microseconds, the 
		// listener knows  that  the  talker  is  trying  to  signal  EOI.    EOI,  which  formally  
		// stands  for  "End  of  Indicator," means  "this  character  will  be  the  last  one."    
		// If  it's  a  sequential  disk  file,  don't  ask  for  more:  there will be no more.  If it's 
		// a relative record, that's the end of the record.  The character itself will still be coming, but 
		// the listener should note: here comes the last character. So if the listener sees the 200 microsecond  
		// time-out,  it  must  signal  "OK,  I  noticed  the  EOI"  back  to  the  talker,    I  does  this  
		// by pulling  the  Data  line  true  for  at  least  60  microseconds,  and  then  releasing  it.  
		// The  talker  will  then revert to transmitting the character in the usual way; within 60 microseconds 
		// it will pull the Clock line  true,  and  transmission  will  continue.  At  this point,  the  Clock  
		// line  is  true  whether  or  not  we have gone through the EOI sequence; we're back to a common 
		// transmission sequence.

		_iec_state or_eq eoiFlag; // or_eq, |=

		// Acknowledge by pull down data more than 60 us
		pull(IEC_PIN_DATA);
		fnSystem.delay_microseconds(TIMING_BIT);
		release(IEC_PIN_DATA);

		// talker should pull clock in response
		if (timeoutWait(IEC_PIN_CLK, pulled))
		{
			Debug_println("receiveByte: talker should pull clk to continue EOI");
			return -1;
		}
			
	}

	// STEP 3: SENDING THE BITS (We are listener now)
	// The talker has eight bits to send.  They will go out without handshake; in other words, 
	// the listener had better be there to catch them, since the talker won't wait to hear from the listener.  At this 
	// point, the talker controls both lines, Clock and Data.  At the beginning of the sequence, it is holding the 
	// Clock true, while the Data line is released to false.  the Data line will change soon, since we'll send the data 
	// over it. The eights bits will go out from the character one at a time, with the least significant bit going first.  
	// For example, if the character is the ASCII question mark, which is  written  in  binary  as  00011111,  the  ones  
	// will  go out  first,  followed  by  the  zeros.  Now,  for  each bit, we set the Data line true or false according 
	// to whether the bit is one or zero.  As soon as that's set, the Clock line is released to false, signalling "data ready."  
	// The talker will typically have a bit in  place  and  be  signalling  ready  in  70  microseconds  or  less.  Once  
	// the  talker  has  signalled  "data ready," it will hold the two lines steady for at least 20 microseconds timing needs 
	// to be increased to 60  microseconds  if  the  Commodore  64  is  listening,  since  the  64's  video  chip  may  
	// interrupt  the processor for 42 microseconds at a time, and without the extra wait the 64 might completely miss a 
	// bit. The listener plays a passive role here; it sends nothing, and just watches.  As soon as it sees the Clock line 
	// false, it grabs the bit from the Data line and puts it away.  It then waits for the clock line to go true, in order 
	// to prepare for the next bit. When the talker figures the data has been held for a sufficient  length  of  time,  it  
	// pulls  the  Clock  line true  and  releases  the  Data  line  to  false.    Then  it starts to prepare the next bit.

	// Get the bits, sampling on clock rising edge, logic 0,0V to logic 1,5V:
	int data = 0;
	set_pin_mode(IEC_PIN_DATA, gpio_mode_t::GPIO_MODE_INPUT);
	for (n = 0; n < 8; n++)
	{
		data >>= 1;

		// wait for bit to be ready to read
		if (timeoutWait(IEC_PIN_CLK, released)) // look for rising edge
		{
			Debug_println("receiveByte: wait for ready to read");
			return -1;
		}

		// get bit
		data or_eq (get_bit(IEC_PIN_DATA) ? (1 << 7) : 0); // read bit and shift in LSB first

		// wait for talker to finish sending bit
		if (timeoutWait(IEC_PIN_CLK, pulled)) // wait for falling edge
		{
			Debug_printf("receiveByte: wait for talker to finish sending (%d) bit [%d] (CLOCK)\r\n", n, data);
			return -1;
		}
	}
	fnSystem.delay_microseconds(TIMING_BIT);

	// STEP 4: FRAME HANDSHAKE (We are listener now)
	// After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true  
	// and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data 
	// line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within  
	// one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

	// Acknowledge byte received
	pull(IEC_PIN_DATA);

	// STEP 5: START OVER (We are listener now)
	// We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,  
	// and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has 
	// happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause, 
	// the Clock and Data lines are released to false and transmission stops.
	// NOTE: This does not seem to hold true for the listener. Listener remains pulling data after EOI (James Johnston)

	// if(_iec_state bitand eoiFlag)
	// {
	// 	// EOI Received
	// 	fnSystem.delay_microseconds(TIMING_STABLE_WAIT);
	// 	release(IEC_PIN_CLK);
	// 	release(IEC_PIN_DATA);
	// }

	return data;
} // receiveByte


// (Jim Butterfield - Compute! July 1983 - "HOW THE VIC/64 SERIAL BUS WORKS")
// STEP 1: READY TO SEND (We are talker now)
// Sooner or later, the talker will want to talk, and send a character. 
// When it's ready to go, it releases the Clock line to false.  This signal change might be 
// translated as "I'm ready to send a character." The listener must detect this and respond, 
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's  
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's  
// a printer chugging out a line of print, or a disk drive with a formatting job in progress, 
// it might holdback for quite a while; there's no time limit. 
bool iecBus::sendByte(int data, bool signalEOI)
{
	// Say we're ready
	release(IEC_PIN_CLK);

	// Wait for listener to be ready
	// STEP 2: READY FOR DATA (We are talker now)
	// When  the  listener  is  ready  to  listen,  it  releases  the  Data  
	// line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false 
	// only when all listeners have released it - in other words, when  all  listeners  are  ready  
	// to  accept  data.  What  happens  next  is  variable. 
	if (timeoutWait(IEC_PIN_DATA, released))
	{
		Debug_printf("sendByte: wait for listener to be ready\r\n");
		return -1;
	}

	// Either  the  talker  will pull the 
	// Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it  
	// will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass  
	// without  the Clock line going to true, it has a special task to perform: note EOI.
	if (signalEOI == true)
	{
		// INTERMISSION: EOI (We are talker now)
		// If the Ready for Data signal isn't acknowledged by the talker within 200 microseconds, the 
		// listener knows  that  the  talker  is  trying  to  signal  EOI.    EOI,  which  formally  
		// stands  for  "End  of  Indicator," means  "this  character  will  be  the  last  one."    
		// If  it's  a  sequential  disk  file,  don't  ask  for  more:  there will be no more.  If it's 
		// a relative record, that's the end of the record.  The character itself will still be coming, but 
		// the listener should note: here comes the last character. So if the listener sees the 200 microsecond  
		// time-out,  it  must  signal  "OK,  I  noticed  the  EOI"  back  to  the  talker,    I  does  this  
		// by pulling  the  Data  line  true  for  at  least  60  microseconds,  and  then  releasing  it.  
		// The  talker  will  then revert to transmitting the character in the usual way; within 60 microseconds 
		// it will pull the Clock line  true,  and  transmission  will  continue.  At  this point,  the  Clock  
		// line  is  true  whether  or  not  we have gone through the EOI sequence; we're back to a common 
		// transmission sequence.

		// Signal eoi by waiting 200 us
		fnSystem.delay_microseconds(TIMING_EOI_WAIT);

		// get eoi acknowledge: pull
		if (timeoutWait(IEC_PIN_DATA, pulled))
		{
			Debug_printf("sendByte: wait for listener acknowledge EOI (data pull)\r\n");
			return -1;
		}

		// get eoi acknowledge: release
		if (timeoutWait(IEC_PIN_DATA, released))
		{
			Debug_printf("sendByte: wait for listener acknowledge EOI (data release)\r\n");
			return -1;
		}
	}
	else
	{
		fnSystem.delay_microseconds(TIMING_NO_EOI);		
	}

	// STEP 3: SENDING THE BITS (We are talker now)
	// The talker has eight bits to send.  They will go out without handshake; in other words, 
	// the listener had better be there to catch them, since the talker won't wait to hear from the listener.  At this 
	// point, the talker controls both lines, Clock and Data.  At the beginning of the sequence, it is holding the 
	// Clock true, while the Data line is released to false.  the Data line will change soon, since we'll sendthe data 
	// over it. The eights bits will go out from the character one at a time, with the least significant bit going first.  
	// For example, if the character is the ASCII question mark, which is  written  in  binary  as  00011111,  the  ones  
	// will  go out  first,  followed  by  the  zeros.  Now,  for  each bit, we set the Data line true or false according 
	// to whether the bit is one or zero.  As soon as that's set, the Clock line is released to false, signalling "data ready."  
	// The talker will typically have a bit in  place  and  be  signalling  ready  in  70  microseconds  or  less.  Once  
	// the  talker  has  signalled  "data ready," it will hold the two lines steady for at least 20 microseconds timing needs 
	// to be increased to 60  microseconds  if  the  Commodore  64  is  listening,  since  the  64's  video  chip  may  
	// interrupt  the processor for 42 microseconds at a time, and without the extra wait the 64 might completely miss a 
	// bit. The listener plays a passive role here; it sends nothing, and just watches.  As soon as it sees the Clock line 
	// false, it grabs the bit from the Data line and puts it away.  It then waits for the clock line to go true, in order 
	// to prepare for the next bit. When the talker figures the data has been held for a sufficient  length  of  time,  it  
	// pulls  the  Clock  line true  and  releases  the  Data  line  to  false.    Then  it starts to prepare the next bit.
	// NOTE: delay between bits needs to be 75us minimum from my observations (James Johnston)

	// Send the bits, sampling on clock rising edge, logic 0,0V to logic 1,5V:
	set_pin_mode(IEC_PIN_DATA, gpio_mode_t::GPIO_MODE_OUTPUT);
	for (int n = 0; n < 8; n++)
	{
		// tell listener to wait
		pull(IEC_PIN_CLK);

		// set data bit
		set_bit(IEC_PIN_DATA, (data & 1));
		fnSystem.delay_microseconds(TIMING_BIT);	 // hold data

		// tell listener bit is ready to read
		release(IEC_PIN_CLK);						 // rising edge
		fnSystem.delay_microseconds(TIMING_BIT);

		data >>= 1; // get next bit
	}

	pull(IEC_PIN_CLK);	// pull clock cause we're done
	release(IEC_PIN_DATA); // release data because we're done

	// STEP 4: FRAME HANDSHAKE (We are talker now)
	// After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true  
	// and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data 
	// line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within  
	// one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

	// Wait for listener to accept data
	if (timeoutWait(IEC_PIN_DATA, pulled))
	{
		Debug_printf("sendByte: wait for listener to accept data\r\n");
		return -1;
	}

	// STEP 5: START OVER (We are talker now)
	// We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,  
	// and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has 
	// happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause, 
	// the Clock and Data lines are released to false and transmission stops. 

	if (signalEOI == true)
	{
		// EOI Sent
		fnSystem.delay_microseconds(TIMING_STABLE_WAIT);
		release(IEC_PIN_CLK);
		release(IEC_PIN_DATA);
	}

	return true;
} // sendByte


// (Jim Butterfield - Compute! July 1983 - "HOW THE VIC/64 SERIAL BUS WORKS")
// TURNAROUND
// An unusual sequence takes place following ATN if the computer wishes the remote device to
// become a talker. This will usually take place only after a Talk command has been sent.
// Immediately after ATN is released, the selected device will be behaving like a listener. After all, it's
// been listening during the ATN cycle, and the computer
// has been a talker. At this instant, we have "wrong way" logic; the device is holding down the Data
// line, and the computer is holding the Clock line. We must turn this around. Here's the sequence:
// the computer quickly realizes what's going on, and pulls the Data line to true (it's already there), as
// well as releasing the Clock line to false. The device waits for this: when it sees the Clock line go
// true [sic], it releases the Data line (which stays true anyway since the computer is now holding it down)
// and then pulls down the Clock line. We're now in our starting position, with the talker (that's the
// device) holding the Clock true, and the listener (the computer) holding the Data line true. The
// computer watches for this state; only when it has gone through the cycle correctly will it be ready
// to receive data. And data will be signalled, of course, with the usual sequence: the talker releases
// the Clock line to signal that it's ready to send.
bool iecBus::turnAround(void)
{
	// Wait until clock is released
	if (timeoutWait(IEC_PIN_CLK, released))
	{
		Debug_println("\r\nturnAround: timeout");
		return false;
	}

	release(IEC_PIN_DATA);
	fnSystem.delay_microseconds(TIMING_BIT);
	pull(IEC_PIN_CLK);
	fnSystem.delay_microseconds(TIMING_BIT);

	Debug_println("\r\nturnAround: complete");
	return true;
} // turnAround


// this routine will set the direction on the bus back to normal
// (the way it was when the computer was switched on)
bool iecBus::undoTurnAround(void)
{
	pull(IEC_PIN_DATA);
	fnSystem.delay_microseconds(TIMING_BIT);
	release(IEC_PIN_CLK);
	fnSystem.delay_microseconds(TIMING_BIT);

	// wait until the computer pulls the clock line
	if (timeoutWait(IEC_PIN_CLK, pulled))
	{
		Debug_printf("\r\nundoTurnAround: timeout");
		return false;
	}

	Debug_printf("\r\nundoTurnAround: complete");
	return true;
} // undoTurnAround

// timeoutWait returns true if timed out
bool iecBus::timeoutWait(int pin, IECline state)
{
	uint16_t t = 0;

	while(t < TIMEOUT) 
	{
		// Check the waiting condition:
		if(status(pin) == state)
		{
			// Got it!  Continue!
			return false;
		}
		fnSystem.delay_microseconds(1); // The aim is to make the loop at least 3 us
		t++;
	}

	// If down here, we have had a timeout.
	// Release lines and go to inactive state with error flag
	release(IEC_PIN_CLK);
	release(IEC_PIN_DATA);

	_iec_state = errorFlag;

	// Wait for ATN release, problem might have occured during attention
	while(status(IEC_PIN_ATN) == pulled);

	// Note: The while above is without timeout. If ATN is held low forever,
	//       the CBM is out in the woods and needs a reset anyways.

	Debug_printf("\r\ntimeoutWait: true [%d] [%d] [%d] [%d] ", pin, state, t, _iec_state);
	return true;
} // timeoutWait

/******************************************************************************
 *                                                                             *
 *                               Public functions                              *
 *                                                                             *
 ******************************************************************************/

// This function checks and deals with atn signal commands
//
// If a command is recieved, the ATN.dataing is saved in ATN. Only commands
// for *this* device are dealt with.
//
// (Jim Butterfield - Compute! July 1983 - "HOW THE VIC/64 SERIAL BUS WORKS")
// ATN SEQUENCES
// When ATN is pulled true, everybody stops what they are doing. The processor will quickly pull the
// Clock line true (it's going to send soon), so it may be hard to notice that all other devices release the
// Clock line. At the same time, the processor releases the Data line to false, but all other devices are
// getting ready to listen and will each pull Data to true. They had better do this within one
// millisecond (1000 microseconds), since the processor is watching and may sound an alarm ("device
// not available") if it doesn't see this take place. Under normal circumstances, transmission now
// takes place as previously described. The computer is sending commands rather than data, but the
// characters are exchanged with exactly the same timing and handshakes as before. All devices
// receive the commands, but only the specified device acts upon it. This results in a curious
// situation: you can send a command to a nonexistent device (try "OPEN 6,6") - and the computer
// will not know that there is a problem, since it receives valid handshakes from the other devices.
// The computer will notice a problem when you try to send or receive data from the nonexistent
// device, since the unselected devices will have dropped off when ATN ceased, leaving you with
// nobody to talk to.


// Set all IEC_signal lines in the correct mode for power up state
void iecBus::setup()
{
	// the I/O signaling method used by this low level driver uses two states:
	// PULL state is pin set to GPIO_MODE_OUTPUT with the output driving DIGI_LOW (0V)
	// RELEASE state is pin set to GPIO_MODE_INPUT so it doesn't drive the bus
	// and it allows the C64 pullup to do its job

	// The CLOCK and DATA lines are bidirectional
	// The ATN line is input only for peripherals
	// The SQR line is output only for peripherals

    Debug_println("IEC SETUP");

	// set up IO states
	pull(IEC_PIN_ATN);
	pull(IEC_PIN_CLK);
	pull(IEC_PIN_DATA);
	pull(IEC_PIN_SRQ);

	// initial pin modes in GPIO
	set_pin_mode(IEC_PIN_ATN, gpio_mode_t::GPIO_MODE_INPUT);
	set_pin_mode(IEC_PIN_CLK, gpio_mode_t::GPIO_MODE_INPUT);
	set_pin_mode(IEC_PIN_DATA, gpio_mode_t::GPIO_MODE_INPUT);	
	set_pin_mode(IEC_PIN_SRQ, gpio_mode_t::GPIO_MODE_INPUT);
	set_pin_mode(IEC_PIN_RESET, gpio_mode_t::GPIO_MODE_INPUT);

#ifdef SPLIT_LINES
	set_pin_mode(IEC_PIN_CLK_OUT, gpio_mode_t::GPIO_MODE_OUTPUT);
	set_pin_mode(IEC_PIN_DATA_OUT, gpio_mode_t::GPIO_MODE_OUTPUT);
#endif

	_iec_state = noFlags;
}

// Primary IEC serivce loop:
// Checks if CBM is sending an attention message. If this is the case,
// the message is recieved and stored in ATN.
void iecBus::service()
{
	ATNMode ret = ATN_IDLE;

#ifdef DEBUG_TIMING
	int pin = IEC_PIN_ATN;
	pull(pin);
	fnSystem.delay_microseconds(1000); // 1000
	release(pin);
	fnSystem.delay_microseconds(1000);

	//pin = IEC_PIN_CLK;
	pull(pin);
	fnSystem.delay_microseconds(20); // 20
	release(pin);
	fnSystem.delay_microseconds(1);

	//pin = IEC_PIN_DATA;
	pull(pin);
	fnSystem.delay_microseconds(50); // 50
	release(pin);
	fnSystem.delay_microseconds(1);

	//pin = IEC_PIN_SRQ;
	pull(pin);
	fnSystem.delay_microseconds(60); // 60
	release(pin);
	fnSystem.delay_microseconds(1);

	//pin = IEC_PIN_ATN;
	pull(pin);
	fnSystem.delay_microseconds(100); // 100
	release(pin);
	fnSystem.delay_microseconds(1);

	//pin = IEC_PIN_CLK;
	pull(pin);
	fnSystem.delay_microseconds(200); // 200
	release(pin);
	fnSystem.delay_microseconds(1);
#endif

	// Checks if CBM is sending a reset (setting the RESET line high). This is typically
	// when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
	if(status(IEC_PIN_RESET) == pulled) 
	{
		if (status(IEC_PIN_ATN) == pulled)
		{
			// If RESET & ATN are both pulled then CBM is off
			ATN.mode = ATN_IDLE;
		}
		
		ATN.mode = ATN_RESET;
		reset();
		fnSystem.delay(1000);
	}

	if (status(IEC_PIN_ATN) == pulled)
	{
		// Attention line is pulled, go to listener mode and get message.
		// Being fast with the next two lines here is CRITICAL!
		pull(IEC_PIN_DATA);
		release(IEC_PIN_CLK);
		fnSystem.delay_microseconds(TIMING_ATN_PREDELAY);

		// Get first ATN byte, it is either LISTEN or TALK
		ATNCommand c = (ATNCommand)receive();
		Debug_printf("\r\ncheckATN: %.2X ", c);
		if (_iec_state bitand errorFlag)
		{
			Debug_printf("\r\ncheckATN: get first ATN byte");
			ATN.mode = ATN_ERROR;
		}

		ATN.code = c;

		ATNCommand cc = c;
		if (c != ATN_COMMAND_UNTALK && c != ATN_COMMAND_UNLISTEN)
		{
			// Is this a Listen or Talk command
			cc = (ATNCommand)(c bitand ATN_COMMAND_LISTEN);
			if (cc == ATN_COMMAND_LISTEN)
			{
				ATN.device_id = c ^ ATN_COMMAND_LISTEN; // device specified, '^' = XOR
			}
			else
			{
				cc = (ATNCommand)(c bitand ATN_COMMAND_TALK);
				ATN.device_id = c ^ ATN_COMMAND_TALK; // device specified
			}

			// Get the first cmd byte, the ATN.code
			c = (ATNCommand)receive();
			if (_iec_state bitand errorFlag)
			{
				Debug_printf("\r\ncheckATN: get first cmd byte");
				ATN.mode = ATN_ERROR;
			}

			ATN.code = c;
			ATN.command = c bitand 0xF0; // upper nibble, the command itself
			ATN.channel = c bitand 0x0F; // lower nibble is the channel
		}

		if (isDeviceEnabled(ATN.device_id))
		{
			if (cc == ATN_COMMAND_LISTEN) // 0x20 LISTEN + device number (0-30)
			{
				listen();
			}
			else if (cc == ATN_COMMAND_TALK) // 0x40 TALK + device number (0-30)
			{
				talk();
			}
			else
			{
				// Either the message is not for us or insignificant, like unlisten.
				fnSystem.delay_microseconds(TIMING_ATN_DELAY);
				release(IEC_PIN_DATA);
				release(IEC_PIN_CLK);

				if (cc == ATN_COMMAND_UNLISTEN) // 3F UNLISTEN
				{
					Debug_printf("(UNLISTEN)", cc);
				}
				if (cc == ATN_COMMAND_UNTALK) // 5F UNTALK
				{	
					Debug_printf("(UNTALK)", cc);
				}

				// Wait for ATN to release and quit
				while(status(IEC_PIN_ATN) == released);
				Debug_printf("\r\ncheckATN: ATN Released\r\n");
			}
		}

		// some delay is required before more ATN business can take place.
		fnSystem.delay_microseconds(TIMING_ATN_DELAY);
	}
	// else
	// {
	// 	// No ATN, keep lines in a released state.
	// 	release(IEC_PIN_DATA);
	// 	release(IEC_PIN_CLK);
	// }


    // Go process the command
    _iec_process_cmd();

    // // Go check if the modem needs to read data if it's active
    // if (_modemDev != nullptr && _modemDev->modemActive)
    // {
    //     _modemDev->sio_handle_modem();
    // }

    // // Handle interrupts from network protocols
    // for (int i = 0; i < 8; i++)
    // {
    //     if (_netDev[i] != nullptr)
    //         _netDev[i]->sio_poll_interrupt();
    // }
}

// Reset all devices on the bus
void iecBus::reset()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Resetting device %02x\n",devicep->device_id());
        devicep->reset();
    }
    Debug_printf("All devices reset.\n");
}

// Give devices an opportunity to clean up before a reboot
void iecBus::shutdown()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->device_id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

// Should avoid using this as it requires counting through the list
int iecBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

// Add device to IEC bus
void iecBus::addDevice(iecDevice *pDevice, int device_id)
{
    // if (device_id == DEVICEID_FUJINET)
    // {
    //     _fujiDev = (iecFuji *)pDevice;
    // }
    // else if (device_id == DEVICEID_RS232)
    // {
    //     _modemDev = (iecModem *)pDevice;
    // }
    // else if (device_id >= DEVICEID_FN_NETWORK && device_id <= DEVICEID_FN_NETWORK_LAST)
    // {
    //     _netDev[device_id - DEVICEID_FN_NETWORK] = (iecNetwork *)pDevice;
    // }
    // else if (device_id == DEVICEID_MIDI)
    // {
    //     _midiDev = (iecMIDIMaze *)pDevice;
    // }
    // else if (device_id == DEVICEID_CASSETTE)
    // {
    //     _cassetteDev = (iecCassette *)pDevice;
    // }
    // else if (device_id == DEVICEID_CPM)
    // {
    //     _cpmDev = (iecCPM *)pDevice;
    // }
    // else if (device_id == DEVICEID_PRINTER && device_id <= DEVICEID_PRINTER_LAST)
    // {
    //     _printerdev = (iecPrinter *)pDevice;
    // }

    pDevice->_device_id = device_id;

    _daisyChain.push_front(pDevice);
}

// Removes device from the SIO bus.
// Note that the destructor is called on the device!
void iecBus::remDevice(iecDevice *p)
{
    _daisyChain.remove(p);
}

iecDevice *iecBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_device_id == device_id)
            return devicep;
    }
    return nullptr;
}

void iecBus::changeDeviceId(iecDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_device_id = device_id;
    }
}





// IEC_receive receives a byte
//
int iecBus::receive()
{
	int data;
	data = receiveByte();
	return data;
} // receive


// IEC_send sends a byte
//
bool iecBus::send(uint8_t data)
{
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif
	return sendByte(data, false);
} // send

bool iecBus::send(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
	{
		if (!send(data[i]))
			return false;
	}
	return true;
}


// Same as IEC_send, but indicating that this is the last byte.
//
bool iecBus::sendEOI(uint8_t data)
{
	Debug_printf("\r\nEOI Sent!");
	if (sendByte(data, true))
	{
		// As we have just send last byte, turn bus back around
		if (undoTurnAround())
		{
			return true;
		}
	}

	return false;
} // sendEOI


// A special send command that informs file not found condition
//
bool iecBus::sendFNF()
{
	// Message file not found by just releasing lines
	release(IEC_PIN_DATA);
	release(IEC_PIN_CLK);

	// Hold back a little...
	fnSystem.delay_microseconds(TIMING_FNF_DELAY);

	Debug_printf("\r\nsendFNF: true");
	return true;
} // sendFNF


bool iecBus::isDeviceEnabled(const int deviceNumber)
{
	return (enabledDevices & (1 << deviceNumber));
} // isDeviceEnabled

void iecBus::enableDevice(const int deviceNumber)
{
	enabledDevices ^= (-1 ^ enabledDevices) & (1UL << deviceNumber);
	return;
} // enableDevice

void iecBus::disableDevice(const int deviceNumber)
{
	enabledDevices &= ~(1UL << deviceNumber);
	return;
} // disableDevice

IECState iecBus::state() const
{
	return static_cast<IECState>(_iec_state);
} // state

iecBus IEC; // Global IEC object

#endif // BUILD_CBM