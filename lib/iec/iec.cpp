#include "../../include/debug.h"

#include "iec.h"

using namespace CBM;


IEC::IEC() :
	m_state(noFlags)
{
} // ctor

// Set all IEC_signal lines in the correct mode for power up state
bool IEC::init()
{
	// the I/O signaling method used by this low level driver uses two states:
	// PULL state is pin set to GPIO_MODE_OUTPUT with the output driving DIGI_LOW (0V)
	// RELEASE state is pin set to GPIO_MODE_INPUT so it doesn't drive the bus
	// and it allows the C64 pullup to do its job

	// The CLOCK and DATA lines are bidirectional
	// The ATN line is input only for peripherals
	// The SQR line is output only for peripherals

	// set up IO states
	readATN();
	writeCLOCK(release);
	writeDATA(release);
	writeSRQ(release);

	// TODO:
	//#ifdef RESET_C64
	//	fnSystem.set_pin_mode(m_resetPin, gpio_mode_t::GPIO_MODE_OUTPUT);
	//	fnSystem.digital_write(m_resetPin, false);	// only early C64's could be reset by a slave going high.
	//#endif
	//	fnSystem.set_pin_mode(IEC_PIN_RESET, gpio_mode_t::GPIO_MODE_INPUT);

	m_state = noFlags;

	return true;
} // init

// timeoutWait returns true if timed out
bool IEC::timeoutWait(int waitBit, IECline waitFor)
{
	uint16_t t = 0;
	IECline c;

	//	ESP.wdtFeed();
	while (t < TIMEOUT)
	{
		// Check the waiting condition:
		c = readPIN(waitBit);

		if (c == waitFor)
		{
			//Debug_println("timeoutWait: false");
			return false;
		}

		fnSystem.delay_microseconds(1); // The aim is to make the loop at least 3 us
		t++;
	}
	// If down here, we have had a timeout.
	// Release lines and go to inactive state with error flag
	writeCLOCK(release);
	writeDATA(release);

	m_state = errorFlag;

	// Wait for ATN release, problem might have occured during attention
	//while(not readATN());

	// Note: The while above is without timeout. If ATN is held low forever,
	//       the CBM is out in the woods and needs a reset anyways.

	Debug_printf("\r\ntimeoutWait: true [%d] [%d] [%d] [%d]", waitBit, waitFor, t, m_state);
	return true;
} // timeoutWait


// IEC Recieve byte standard function
//
// Returns data recieved
// Might set flags in iec_state
//
/*	 from Derogee's "IEC Disected"
STEP 1: READY TO SEND
Sooner or later, the talker will want to talk, and send a character. When it's ready to go, it releases
the Clock line to false. This signal change might be translated as "I'm ready to send a character."
The listener must detect this and respond, but it doesn't have to do so immediately. The listener will
respond to the talker's "ready to send" signal whenever it likes; it can wait a long time. If it's a
printer chugging out a line of print, or a disk drive with a formatting job in progress, it might hold
back for quite a while; there's no time limit.
 */

// FIXME: m_iec might be better returning bool and returning read byte as reference in order to indicate any error.
// or make the error negative int and valid data positive byte < 256
int IEC::receiveByte(void)
{
	m_state = noFlags;

	// Wait for talker ready
	if (timeoutWait(IEC_PIN_CLOCK, release))
		return -1; // return error because timeout

	// Say we're ready
	writeDATA(release);

	/* from Derogee's "IEC Disected"
	INTERMISSION: EOI
	If the Ready for Data signal isn't acknowledged by the talker within 200 microseconds, the listener
	knows that the talker is trying to signal EOI. EOI, which formally stands for "End of Indicator,"
	means "this character will be the last one."
	*/
	// Record how long CLOCK is high, more than 200 us means EOI
	int n = 0;
	while ((readCLOCK() == release) and (n < 20))
	{
		fnSystem.delay_microseconds(10);  // this loop should cycle in about 10 us...
		n++;
	}
	/*  from Derogee's "IEC Disected"
	So if the listener sees the 200
	microsecond time-out, it must signal "OK, I noticed the EOI" back to the talker, it does this by
	pulling the Data line true for at least 60 microseconds, and then releasing it. The talker will then
	revert to transmitting the character in the usual way; within 60 microseconds it will pull the Clock
	line true, and transmission will continue. At this point, the Clock line is true whether or not we
	have gone through the EOI sequence; we're back to a common transmission sequence.
	*/
	if (n >= TIMING_EOI_THRESH)
	{
		// EOI intermission
		m_state or_eq eoiFlag; // or_eq, |=

		// Acknowledge by pull down data more than 60 us
		writeDATA(pull);
		fnSystem.delay_microseconds(TIMING_BIT);
		writeDATA(release);
	}

	// for clk
	if (timeoutWait(IEC_PIN_CLOCK, pull))
		return 0;

	// TODO: why?
	// Sample ATN
	if(false == readATN())
		m_state or_eq atnFlag;

	int data = 0;
	// Get the bits, sampling on clock rising edge, logic 0,0V to logic 1,5V:
	for(n = 0; n < 8; n++) {
		data >>= 1;
		if (timeoutWait(IEC_PIN_CLOCK, release)) // look for rising edge
			return -1;
		data or_eq (readDATA() == pull ? (1 << 7) : 0); // read bit and shift in LSB first
		if (timeoutWait(IEC_PIN_CLOCK, pull))			// wait for falling edge
			return -1;
	}
	//Debug_printf("%.2X ", data);

	/*
	STEP 4: FRAME HANDSHAKE
	After the eighth bit has been sent, it's the listener's turn to acknowledge. At this moment, the Clock
	line is true and the Data line is false. The listener must acknowledge receiving the byte OK by
	pulling the Data line to true. The talker is now watching the Data line. If the listener doesn't pull
	the Data line true within one millisecond - one thousand microseconds - it will know that
	something's wrong and may alarm appropriately.
	*/
	// Signal we accepted data:
	writeDATA(pull);

	return data;
} // receiveByte


// IEC Send byte standard function
//
// Sends the byte and can signal EOI
//
bool IEC::sendByte(int data, bool signalEOI)
{
	// //Listener must have accepted previous data
	// if(timeoutWait(IEC_PIN_DATA, true))
	// 	return false;

	// Say we're ready
	writeCLOCK(release);

	// Wait for listener to be ready
	if(timeoutWait(IEC_PIN_DATA, false))
		return false;

	if(signalEOI) {
		// FIXME: Make this like sd2iec and may not need a fixed delay here.
		//Debug_print("{EOI}");

		// Signal eoi by waiting 200 us
		fnSystem.delay_microseconds(TIMING_EOI_WAIT);

		// get eoi acknowledge:
		if(timeoutWait(IEC_PIN_DATA, true))
			return false;

		if(timeoutWait(IEC_PIN_DATA, false))
			return false;
	}

	fnSystem.delay_microseconds(TIMING_NO_EOI);

	// Send bits
	//	ESP.wdtFeed();
	for(int n = 0; n < 8; n++) {
		// FIXME: Here check whether data pin goes low, if so end (enter cleanup)!

		writeCLOCK(pull);
		// set data
		writeDATA((data bitand 1) ? pull : release);

		fnSystem.delay_microseconds(TIMING_BIT);
		writeCLOCK(release);
		fnSystem.delay_microseconds(TIMING_BIT);

		data >>= 1;
	}

	writeCLOCK(pull);
	writeDATA(release);

	// FIXME: Maybe make the following ending more like sd2iec instead.

	// Line stabilization delay
//	fnSystem.delay_microseconds(TIMING_STABLE_WAIT);

	// Wait for listener to accept data
	if(timeoutWait(IEC_PIN_DATA, true))
		return false;

	return true;
} // sendByte


// IEC turnaround
bool IEC::turnAround(void)
{
	Debug_printf("\r\nturnAround: ");

	// Wait until clock is released
	if(timeoutWait(IEC_PIN_CLOCK, false))
	{
		Debug_print("false");
		return false;
	}
		

	writeDATA(false);
	fnSystem.delay_microseconds(TIMING_BIT);
	writeCLOCK(true);
	fnSystem.delay_microseconds(TIMING_BIT);

	Debug_print("true");
	return true;
} // turnAround


// this routine will set the direction on the bus back to normal
// (the way it was when the computer was switched on)
bool IEC::undoTurnAround(void)
{
	writeDATA(true);
	fnSystem.delay_microseconds(TIMING_BIT);
	writeCLOCK(false);
	fnSystem.delay_microseconds(TIMING_BIT);

	Debug_printf("\r\nundoTurnAround:");

	// wait until the computer releases the clock line
	if(timeoutWait(IEC_PIN_CLOCK, true))
	{
		Debug_print("false");
		return false;
	}

	Debug_print("true");
	return true;
} // undoTurnAround


/******************************************************************************
 *                                                                             *
 *                               Public functions                              *
 *                                                                             *
 ******************************************************************************/

// This function checks and deals with atn signal commands
//
// If a command is recieved, the atn_cmd.string is saved in atn_cmd. Only commands
// for *this* device are dealt with.
//
/** from Derogee's "IEC Disected"
 * ATN SEQUENCES
 * When ATN is pulled true, everybody stops what they are doing. The processor will quickly pull the
 * Clock line true (it's going to send soon), so it may be hard to notice that all other devices release the
 * Clock line. At the same time, the processor releases the Data line to false, but all other devices are
 * getting ready to listen and will each pull Data to true. They had better do this within one
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
// Return value, see IEC::ATNCheck definition.
IEC::ATNCheck IEC::checkATN(ATNCmd& atn_cmd)
{
	ATNCheck ret = ATN_IDLE;
	int i = 0;

	if (readATN() == pull)
	{
		// Attention line is pulled, go to listener mode and get message.
		// Being fast with the next two lines here is CRITICAL!
		writeDATA(pull);
		writeCLOCK(release);
		fnSystem.delay_microseconds(TIMING_ATN_PREDELAY);

		// Get first ATN byte, it is either LISTEN or TALK
		ATNCommand c = (ATNCommand)receive();
		Debug_printf("\r\ncheckATN: %.2X ", c);
		if(m_state bitand errorFlag)
		{
			Debug_printf("\r\nm_state bitand errorFlag 0");
			return ATN_ERROR;
		}

		atn_cmd.code = c;
		
		ATNCommand cc = c;
		if(c != ATN_CODE_UNTALK && c != ATN_CODE_UNLISTEN)
		{
			// Is this a Listen or Talk command
			cc = (ATNCommand)(c bitand ATN_CODE_LISTEN);
			if(cc == ATN_CODE_LISTEN)
			{
				atn_cmd.device = c ^ ATN_CODE_LISTEN; // device specified
			} 
			else
			{
				cc = (ATNCommand)(c bitand ATN_CODE_TALK);
				atn_cmd.device = c ^ ATN_CODE_TALK; // device specified
			}

			// Get the first cmd byte, the atn_cmd.code
			c = (ATNCommand)receive();
			if(m_state bitand errorFlag)
			{
				Debug_printf("\r\nm_state bitand errorFlag 1");
				return ATN_ERROR;
			}
			
			atn_cmd.code = c;
			atn_cmd.command = c bitand 0xF0; // upper nibble, the command itself
			atn_cmd.channel = c bitand 0x0F; // lower nibble is the channel			
		}

		if ( cc == ATN_CODE_LISTEN && isDeviceEnabled(atn_cmd.device) )
		{
			ret = deviceListen(atn_cmd);
		}
		else if ( cc == ATN_CODE_TALK && isDeviceEnabled(atn_cmd.device) )
		{
			ret = deviceTalk(atn_cmd);
		}
		else 
		{
			// Either the message is not for us or insignificant, like unlisten.
			fnSystem.delay_microseconds(TIMING_ATN_DELAY);
			writeDATA(false);
			writeCLOCK(false);

			if ( cc == ATN_CODE_UNTALK )
				Debug_print("UNTALK");
			if ( cc == ATN_CODE_UNLISTEN )
				Debug_print("UNLISTEN");
			
			Debug_printf(" (%.2d DEVICE)", atn_cmd.device);			

			// Wait for ATN to release and quit
			while(not readATN());
			Debug_printf("\r\ncheckATN: ATN Released\r\n");
		}

		// some delay is required before more ATN business can take place.
		fnSystem.delay_microseconds(TIMING_ATN_DELAY);

		atn_cmd.strLen = i;
	}
	else 
	{
		// No ATN, keep lines in a released state.
		writeDATA(false);
		writeCLOCK(false);
	}

	return ret;
} // checkATN

/**
 * from Derogee's "IEC Disected"
 * TRANSMISSION: STEP ZERO
 * Let's look at the sequence when a character is about to be transmitted. At this time, both the Clock
 * line and the Data line are being held down to the true state. With a test instrument, you can't tell
 * who's doing it, but I'll tell you: the talker is holding the Clock line true, and the listener is holding
 * the Data line true. There could be more than one listener, in which case all of the listeners are
 * holding the Data line true. Each of the signals might be viewed as saying, "I'm here!".
*/
IEC::ATNCheck IEC::deviceListen(ATNCmd& atn_cmd)
{
	int i=0;
	ATNCommand c;

	// Okay, we will listen.
	Debug_printf("(20 LISTEN) (%.2d DEVICE)", atn_cmd.device);

	// If the command is DATA and it is not to expect just a small command on the command channel, then
	// we're into something more heavy. Otherwise read it all out right here until UNLISTEN is received.
	if(atn_cmd.command == ATN_CODE_DATA and atn_cmd.channel not_eq CMD_CHANNEL) 
	{
		// A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
		Debug_printf("\r\ncheckATN: %.2X (DATA)      (%.2X COMMAND) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);
		return ATN_CMD_LISTEN;
	}
	else if(atn_cmd.command not_eq ATN_CODE_UNLISTEN)
	//if(c not_eq ATN_CODE_UNLISTEN)
	{

		if(atn_cmd.command == ATN_CODE_OPEN) 
		{
			Debug_printf("\r\ncheckATN: %.2X (%.2X OPEN) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);
		}
		else if(atn_cmd.command == ATN_CODE_CLOSE) 
		{
			Debug_printf("\r\ncheckATN: %.2X (%.2X CLOSE) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);
		}

		// Some other command. Record the cmd string until UNLISTEN is sent
		for(;;) 
		{
			c = (ATNCommand)receive();
			if(m_state bitand errorFlag)
			{
				Debug_printf("\r\nm_state bitand errorFlag 2");
				return ATN_ERROR;
			}
				

			if((m_state bitand atnFlag) and (ATN_CODE_UNLISTEN == c)) 
			{
				Debug_printf(" [%s]", atn_cmd.str);
				Debug_printf("\r\ncheckATN: %.2X (UNLISTEN)", c);
				break;
			}

			if(i >= ATN_CMD_MAX_LENGTH) 
			{
				// Buffer is going to overflow, this is an error condition
				// FIXME: here we should propagate the error type being overflow so that reading error channel can give right code out.
				Debug_printf("\r\nATN_CMD_MAX_LENGTH");
				return ATN_ERROR;
			}
			atn_cmd.str[i++] = c;
			atn_cmd.str[i] = '\0';
		}
		return ATN_CMD;
	}
	return ATN_IDLE;
}

//IEC::ATNCheck IEC::deviceUnListen(ATNCmd& atn_cmd)
//{
//
//}

IEC::ATNCheck IEC::deviceTalk(ATNCmd& atn_cmd)
{
	int i=0;
	ATNCommand c;

	// Okay, we will talk soon
	Debug_printf("(40 TALK) (%.2d DEVICE)", atn_cmd.device);
	Debug_printf("\r\ncheckATN: %.2X (%.2X SECOND) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);

	while(not readATN()) {
		if(readCLOCK()) {
			c = (ATNCommand)receive();
			if(m_state bitand errorFlag)
				return ATN_ERROR;

			if(i >= ATN_CMD_MAX_LENGTH) {
				// Buffer is going to overflow, this is an error condition
				// FIXME: here we should propagate the error type being overflow so that reading error channel can give right code out.
				return ATN_ERROR;
			}
			atn_cmd.str[i++] = c;
			atn_cmd.str[i] = '\0';
		}
	}

	// Now ATN has just been released, do bus turnaround
	if(not turnAround())
		return ATN_ERROR;

	// We have recieved a CMD and we should talk now:
	return ATN_CMD_TALK;
}

//IEC::ATNCheck IEC::deviceUnTalk(ATNCmd& atn_cmd)
//{
//
//}

//bool IEC::checkRESET()
//{
//	//	return false;
//	//	// hmmm. Is this all todo?
//	return readRESET();
//} // checkRESET


// IEC_receive receives a byte
//
int IEC::receive()
{
	int data;
	data = receiveByte();
	return data;
} // receive


// IEC_send sends a byte
//
bool IEC::send(int data)
{
#ifdef DATA_STREAM
	Debug_printf("%.2X ", data);
#endif
	return sendByte(data, false);
} // send


// Same as IEC_send, but indicating that this is the last byte.
//
bool IEC::sendEOI(int data)
{
	Debug_printf("\r\nEOI Sent!");
	if(sendByte(data, true)) {
		//Debug_print("true");

		// As we have just send last byte, turn bus back around
		if(undoTurnAround())
		{
			return true;
		}
	}

	//Debug_print("false");
	return false;
} // sendEOI


// A special send command that informs file not found condition
//
bool IEC::sendFNF()
{
	// Message file not found by just releasing lines
	writeDATA(false);
	writeCLOCK(false);

	// Hold back a little...
	fnSystem.delay_microseconds(TIMING_FNF_DELAY);

	Debug_printf("\r\nsendFNF: true");
	return true;
} // sendFNF





bool IEC::isDeviceEnabled(const int deviceNumber)
{
	return (enabledDevices & (1<<deviceNumber));
} // isDeviceEnabled

IEC::IECState IEC::state() const
{
	return static_cast<IECState>(m_state);
} // state

IEC iec;
