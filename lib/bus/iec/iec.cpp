#ifdef BUILD_CBM

#include "iec.h"

using namespace CBM; 
using namespace Protocol;


IEC::IEC()
{
	init();
} // ctor

// Set all IEC_signal lines in the correct mode
//
bool IEC::init()
{
	// make sure the output states are initially LOW
	protocol.release(IEC_PIN_ATN);
	protocol.release(IEC_PIN_CLK);
	protocol.release(IEC_PIN_DATA);
	protocol.release(IEC_PIN_SRQ);

	// initial pin modes in GPIO
	pinMode(IEC_PIN_ATN, INPUT);
	pinMode(IEC_PIN_CLK, INPUT);
	pinMode(IEC_PIN_DATA, INPUT);	
	pinMode(IEC_PIN_SRQ, INPUT);
	pinMode(IEC_PIN_RESET, INPUT);

#ifdef SPLIT_LINES
	pinMode(IEC_PIN_CLK_OUT, OUTPUT);
	pinMode(IEC_PIN_DATA_OUT, OUTPUT);
#endif

	protocol.flags = CLEAR;

	return true;
} // init

// IEC turnaround
bool IEC::turnAround(void)
{
	/*
	TURNAROUND
	An unusual sequence takes place following ATN if the computer wishes the remote device to
	become a talker. This will usually take place only after a Talk command has been sent.
	Immediately after ATN is RELEASED, the selected device will be behaving like a listener. After all, it's
	been listening during the ATN cycle, and the computer has been a talker. At this instant, we 
	have "wrong way" logic; the device is holding down the Data	line, and the computer is holding the 
	Clock line. We must turn this around. Here's the sequence:
	the computer quickly realizes what's going on, and pulls the Data line to true (it's already there), as
	well as releasing the Clock line to false. The device waits for this: when it sees the Clock line go
	true [sic], it releases the Data line (which stays true anyway since the computer is now holding it down)
	and then pulls down the Clock line. We're now in our starting position, with the talker (that's the
	device) holding the Clock true, and the listener (the computer) holding the Data line true. The
	computer watches for this state; only when it has gone through the cycle correctly will it be ready
	to receive data. And data will be signalled, of course, with the usual sequence: the talker releases
	the Clock line to signal that it's ready to send.
	*/
	// Debug_printf("IEC turnAround: ");

	// Wait until clock is RELEASED
	while(protocol.status(IEC_PIN_CLK) != RELEASED);
		

	protocol.release(IEC_PIN_DATA);
	delayMicroseconds(TIMING_Tv);
	protocol.pull(IEC_PIN_CLK);
	delayMicroseconds(TIMING_Tv);

	// Debug_println("complete");
	return true;
} // turnAround


// this routine will set the direction on the bus back to normal
// (the way it was when the computer was switched on)
bool IEC::undoTurnAround(void)
{
	protocol.pull(IEC_PIN_DATA);
	delayMicroseconds(TIMING_Tv);
	protocol.release(IEC_PIN_CLK);
	delayMicroseconds(TIMING_Tv);

	// Debug_printf("IEC undoTurnAround: ");

	// wait until the computer protocol.releases the clock line
	while(protocol.status(IEC_PIN_CLK) != RELEASED);

	// Debug_println("complete");
	return true;
} // undoTurnAround


/******************************************************************************
 *                                                                             *
 *                               Public functions                              *
 *                                                                             *
 ******************************************************************************/

// This function checks and deals with atn signal commands
//
// If a command is recieved, the iec_data.string is saved in iec_data. Only commands
// for *this* device are dealt with.
//
/** from Derogee's "IEC Disected"
 * ATN SEQUENCES
 * When ATN is PULLED true, everybody stops what they are doing. The processor will quickly protocol.pull the
 * Clock line true (it's going to send soon), so it may be hard to notice that all other devices protocol.release the
 * Clock line. At the same time, the processor protocol.releases the Data line to false, but all other devices are
 * getting ready to listen and will each protocol.pull Data to true. They had better do this within one
 * millisecond (1000 microseconds), since the processor is watching and may sound an alarm ("device
 * not available") if it doesn't see this take place. Under normal circumstances, transmission now
 * takes place as previously described. The computer is sending commands rather than data, but the
 * characters are exchanged with exactly the same timing and handshakes as before. All devices
 * receive the commands, but only the specified device acts upon it. This results in a curious
 * situation: you can send a command to a nonexistent device (try "OPEN 6,6") - and the computer
 * will not know that there is a problem, since it receives valid handshakes from the other devices.
 * The computer will notice a problem when you try to send or receive data from the nonexistent
 * device, since the unselected devices will have dropped off when ATN ceased, leaving you with
 * nobody to talk to.
 */
// Return value, see IEC::BusState definition.
IEC::BusState IEC::service(Data& iec_data)
{
	IEC::BusState r = BUS_IDLE;

	// Checks if CBM is sending a reset (setting the RESET line high). This is typically
	// when the CBM is reset itself. In this case, we are supposed to reset all states to initial.
	// if(status(IEC_PIN_RESET) == PULLED) 
	// {
	// 	if (status(IEC_PIN_ATN) == PULLED)
	// 	{
	// 		// If RESET & ATN are both PULLED then CBM is off
	// 		return BUS_IDLE;
	// 	}
		
	// 	return BUS_RESET;
	// }


	// Attention line is PULLED, go to listener mode and get message.
	// Being fast with the next two lines here is CRITICAL!
	protocol.release(IEC_PIN_CLK);
	protocol.pull(IEC_PIN_DATA);
	delayMicroseconds(TIMING_Tne);

	// Get command
	Debug_printf("   IEC: [");
	int16_t c = (Command)receive(iec_data.device);
	if(protocol.flags bitand ERROR)
	{
		Debug_printf("Get first ATN byte");
		return BUS_ERROR;
	}
	if(protocol.flags bitand JIFFY_ACTIVE)
	{
		Debug_printf("JIFFY ");
	}

	iec_data.command = c; // bitand 0xFF; // Clear flags in high byte

	// Decode command byte
	if((c bitand 0xF0) == IEC_GLOBAL)
	{
		iec_data.command = IEC_GLOBAL;
		iec_data.device = c xor IEC_GLOBAL;
		iec_data.channel = 0;
		Debug_printf(BACKSPACE "] (00 GLOBAL) (%.2d COMMAND)\r\n", iec_data.device);
	} 
	else if((c bitand 0xF0) == IEC_LISTEN)
	{
		iec_data.command = IEC_LISTEN;
		iec_data.device = c xor IEC_LISTEN;
		iec_data.channel = 0;
		Debug_printf(BACKSPACE "] (20 LISTEN) (%.2d DEVICE) [", iec_data.device);
	} 
	else if(c == IEC_UNLISTEN)
	{
		Debug_printf(BACKSPACE "] (3F UNLISTEN)\r\n");
		releaseLines(false);
		return BUS_IDLE;
	} 
	else if((c bitand 0xF0) == IEC_TALK)
	{
		iec_data.command = IEC_TALK;
		iec_data.device = c xor IEC_TALK;
		iec_data.channel = 0;
		Debug_printf(BACKSPACE "] (40 TALK) (%.2d DEVICE) [", iec_data.device);
	}
	else if(c == IEC_UNTALK)
	{
		Debug_printf(BACKSPACE "] (5F UNTALK)\r\n");
		releaseLines(false);
		return BUS_IDLE;
	} 
	else if((c bitand 0xF0) == IEC_SECOND)
	{
		iec_data.command = IEC_SECOND;
		iec_data.channel = c xor IEC_SECOND;
		Debug_printf(BACKSPACE "] (60 DATA) (%.2d CHANNEL)\r\n", iec_data.channel);
	}
	else if((c bitand 0xF0) == IEC_CLOSE)
	{
		iec_data.command = IEC_CLOSE;
		iec_data.channel = c xor IEC_CLOSE;
		Debug_printf(BACKSPACE "] (EO CLOSE) (%.2d CHANNEL)\r\n", iec_data.channel);
	}
	else if((c bitand 0xF0) == IEC_OPEN)
	{
		iec_data.command = IEC_OPEN;
		iec_data.channel = c xor IEC_OPEN;
		Debug_printf(BACKSPACE "] (FO OPEN) (%.2d CHANNEL)\r\n", iec_data.channel);
	}

	//Debug_printf("command[%.2X] device[%.2d] secondary[%.2d] channel[%.2d]", iec_data.command, iec_data.device, iec_data.secondary, iec_data.channel);

	int8_t cc = iec_data.command;
	// Is this a Listen or Talk command and is it for us?
	if((iec_data.command == IEC_LISTEN || iec_data.command == IEC_TALK) && isDeviceEnabled(iec_data.device))
	{
		// Get the secondary address
		c = receive();
		if(protocol.flags bitand ERROR)
		{
			Debug_printf("Get the first cmd byte");
			return BUS_ERROR;
		}
		
		iec_data.command = c bitand 0xF0; // upper nibble, command
		iec_data.channel = c bitand 0x0F; // lower nibble, channel
		//iec_data.content = { 0 };

		// Clear command string
		iec_data.content.clear();

		if ( cc == IEC_LISTEN )
		{
			r = deviceListen(iec_data);
		}
		else
		{
			r = deviceTalk(iec_data);
		}

		if(protocol.flags bitand ERROR)
		{
			Debug_printf("Listen/Talk ERROR");
			r = BUS_ERROR;
		}
	}
	else
	{
		Debug_println("]");
		releaseLines(false);
		return BUS_IDLE;
	}
			
	// Was there an error?
	if(r == BUS_IDLE || r == BUS_ERROR)
	{
		// Debug_printf("release lines");
		releaseLines();
	}
	// Don't do anything here or it could cause LOAD ERROR!!!

	return r;
} // service

IEC::BusState IEC::deviceListen(Data& iec_data)
{
	byte i=0;

	// Okay, we will listen.
	// Debug_printf("(20 LISTEN) (%.2d DEVICE) ", iec_data.device);

	// If the command is SECONDARY and it is not to expect just a small command on the command channel, then
	// we're into something more heavy. Otherwise read it all out right here until UNLISTEN is received.
	if(iec_data.command == IEC_SECOND && iec_data.channel not_eq CMD_CHANNEL) 
	{
		// A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
		Debug_printf(BACKSPACE "] (%.2X SECONDARY) (%.2X CHANNEL)\r\n", iec_data.command, iec_data.channel);
		return BUS_LISTEN;
	}

	// OPEN
	else if(iec_data.command == IEC_SECOND || iec_data.command == IEC_OPEN) 
	{
		Debug_printf(BACKSPACE "] (%.2X OPEN) (%.2X CHANNEL) [", iec_data.command, iec_data.channel);

		// Some other command. Record the cmd string until UNLISTEN is sent
		delayMicroseconds(200);
		while (1)
		{
			if(protocol.status(IEC_PIN_ATN) == PULLED)
			{
				Debug_printf(BACKSPACE BACKSPACE "\r\n");
				return BUS_IDLE;
			}

			int16_t c = receive();
			if(protocol.flags bitand ERROR)
			{
				Debug_printf("Some other command [%.2X]", c);
				return BUS_ERROR;
			}
				
			if(c == IEC_UNLISTEN)
			{
				mstr::rtrimA0(iec_data.content);
				Debug_printf(BACKSPACE "] [%s] (%.2X UNLISTEN)\r\n", iec_data.content.c_str(), c);
				break;
			}

			if(i >= IEC_CMD_MAX_LENGTH) 
			{
				// Buffer is going to overflow, this is an error condition
				// FIXME: here we should propagate the error type being overflow so that reading error channel can give right code out.
				Debug_printf("IEC_CMD_MAX_LENGTH");
				return BUS_ERROR;
			}
			if(c != 0x0D)
			{
				iec_data.content += (uint8_t)c;
			}
		}		
	}

	// CLOSE Named Channel
	else if(iec_data.command == IEC_CLOSE) 
	{
		Debug_printf(BACKSPACE "] (%.2X CLOSE) (%.2X CHANNEL)\r\n", iec_data.command, iec_data.channel);
		return BUS_COMMAND;
	}

	// Unknown
	else
	{
		Debug_printf(BACKSPACE "] OTHER (%.2X COMMAND) (%.2X CHANNEL) ", iec_data.command, iec_data.channel);
	}

	if( iec_data.content.size() )
		return BUS_COMMAND;
	else
		return BUS_IDLE;
}

// void IEC::deviceUnListen(void)
// {
// 	Debug_printf("");

// 	// Release lines
// 	protocol.release(IEC_PIN_CLK);
// 	protocol.release(IEC_PIN_DATA);

// 	// Wait for ATN to protocol.release and quit
// 	while(protocol.status(IEC_PIN_ATN) == PULLED)
// 	{
// 		ESP.wdtFeed();
// 	}
// }

IEC::BusState IEC::deviceTalk(Data& iec_data)
{
	// Okay, we will talk soon
	Debug_printf(BACKSPACE "] (%.2X SECONDARY) (%d CHANNEL)\r\n", iec_data.command, iec_data.channel);

	// Delay after ATN is RELEASED
	//delayMicroseconds(TIMING_BIT);

	// Now do bus turnaround
	if(not turnAround())
		return BUS_ERROR;

	// We have recieved a CMD and we should talk now:
	return BUS_TALK;
}

// void IEC::deviceUnTalk(void)
// {
// 	Debug_printf("");

// 	// Release lines
// 	protocol.release(IEC_PIN_CLK);
// 	protocol.release(IEC_PIN_DATA);

// 	// Wait for ATN to protocol.release and quit
// 	while(protocol.status(IEC_PIN_ATN) == PULLED)
// 	{
// 		ESP.wdtFeed();
// 	}
// }


void IEC::releaseLines(bool wait)
{
	//Debug_printf("");

	// Release lines
	protocol.release(IEC_PIN_CLK);
	protocol.release(IEC_PIN_DATA);

	// Wait for ATN to release and quit
	if ( wait )
	{
		//Debug_printf("Waiting for ATN to release");
		while(protocol.status(IEC_PIN_ATN) == PULLED)
		{
			ESP.wdtFeed();
		}		
	}
}


// boolean  IEC::checkRESET()
// {
// 	return readRESET();
// 	return false;
// } // checkRESET


// IEC_receive receives a byte
//
int16_t IEC::receive(uint8_t device)
{
	int16_t data;
	data = protocol.receiveByte(device); // Standard CBM Timing
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif
	// if(data < 0)
	// 	protocol.flags = errorFlag;

	return data;
} // receive


// IEC_send sends a byte
//
bool IEC::send(uint8_t data)
{
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif	
	return protocol.sendByte(data, false); // Standard CBM Timing
} // send

bool IEC::send(std::string data)
{
	for (size_t i = 0; i < data.length(); ++i)
		send(data[i]);

	return true;
}


// Same as IEC_send, but indicating that this is the last byte.
//
bool IEC::sendEOI(uint8_t data)
{
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif	
	Debug_println("\r\nEOI Sent!");
	if(protocol.sendByte(data, true)) 
	{
		// As we have just send last byte, turn bus back around
		if(undoTurnAround())
		{
			return true;
		}
	}

	return false;
} // sendEOI


// A special send command that informs file not found condition
//
bool IEC::sendFNF()
{
	// Message file not found by just releasing lines
	protocol.release(IEC_PIN_DATA);
	protocol.release(IEC_PIN_CLK);

	// BETWEEN BYTES TIME
	delayMicroseconds(TIMING_Tbb);

	Debug_println("\r\nFNF Sent!");
	return true;
} // sendFNF


bool IEC::isDeviceEnabled(const uint8_t deviceNumber)
{
	return (enabledDevices & (1<<deviceNumber));
} // isDeviceEnabled

void IEC::enableDevice(const uint8_t deviceNumber)
{
	enabledDevices |= 1UL << deviceNumber;
} // enableDevice

void IEC::disableDevice(const uint8_t deviceNumber)
{
	enabledDevices &= ~(1UL << deviceNumber);
} // disableDevice


uint8_t IEC::state()
{
	return static_cast<uint8_t>(protocol.flags);
} // state


void IEC::debugTiming()
{
	int pin = IEC_PIN_ATN;
	protocol.pull(pin);
	delayMicroseconds(1000); // 1000
	protocol.release(pin);
	delayMicroseconds(1000);

	pin = IEC_PIN_CLK;
	protocol.pull(pin);
	delayMicroseconds(20); // 20
	protocol.release(pin);
	delayMicroseconds(1);

	pin = IEC_PIN_DATA;
	protocol.pull(pin);
	delayMicroseconds(50); // 50
	protocol.release(pin);
	delayMicroseconds(1);

	pin = IEC_PIN_SRQ;
	protocol.pull(pin);
	delayMicroseconds(60); // 60
	protocol.release(pin);
	delayMicroseconds(1);

	pin = IEC_PIN_ATN;
	protocol.pull(pin);
	delayMicroseconds(100); // 100
	protocol.release(pin);
	delayMicroseconds(1);

	pin = IEC_PIN_CLK;
	protocol.pull(pin);
	delayMicroseconds(200); // 200
	protocol.release(pin);
	delayMicroseconds(1);
}

#endif /* BUILD_CBM */