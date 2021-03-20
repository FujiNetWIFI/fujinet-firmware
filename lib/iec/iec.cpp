#include "../../include/debug.h"

#include "iec.h"

using namespace CBM;


IEC::IEC() :
	m_state(noFlags)
{
} // ctor

// Set all IEC_signal lines in the correct mode
//
bool IEC::init()
{
	// make sure the output states are initially LOW.
	fnSystem.set_pin_mode(IEC_PIN_ATN, gpio_mode_t::GPIO_MODE_OUTPUT);
	fnSystem.set_pin_mode(IEC_PIN_DATA, gpio_mode_t::GPIO_MODE_OUTPUT);
	fnSystem.set_pin_mode(IEC_PIN_CLOCK, gpio_mode_t::GPIO_MODE_OUTPUT);
//	fnSystem.set_pin_mode(IEC_PIN_SRQ, gpio_mode_t::GPIO_MODE_OUTPUT);
	fnSystem.digital_write(IEC_PIN_ATN, false);
	fnSystem.digital_write(IEC_PIN_DATA, false);
	fnSystem.digital_write(IEC_PIN_CLOCK, false);
//	fnSystem.digital_write(IEC_PIN_SRQ, true);

//#ifdef RESET_C64
//	fnSystem.set_pin_mode(m_resetPin, gpio_mode_t::GPIO_MODE_OUTPUT);
//	fnSystem.digital_write(m_resetPin, false);	// only early C64's could be reset by a slave going high.
//#endif

	// initial pin modes in GPIO.
	fnSystem.set_pin_mode(IEC_PIN_ATN, gpio_mode_t::GPIO_MODE_INPUT);
	fnSystem.set_pin_mode(IEC_PIN_CLOCK, gpio_mode_t::GPIO_MODE_INPUT);
	fnSystem.set_pin_mode(IEC_PIN_DATA, gpio_mode_t::GPIO_MODE_INPUT);
//	fnSystem.set_pin_mode(IEC_PIN_RESET, gpio_mode_t::GPIO_MODE_INPUT);


	// Set port low, we don't need internal pullup
	// and DDR input such that we release all signals
	// IEC_PORT and_eq compl(IEC_BIT_ATN bitor IEC_BIT_CLOCK bitor IEC_BIT_DATA);
	// IEC_DDR and_eq compl(IEC_BIT_ATN bitor IEC_BIT_CLOCK bitor IEC_BIT_DATA);

	m_state = noFlags;

	return true;
} // init

int IEC::timeoutWait(int waitBit, bool whileHigh)
{
	uint16_t t = 0;
	bool c;
	
//	ESP.wdtFeed();
	while(t < TIMEOUT) {

		// Check the waiting condition:
		c = readPIN(waitBit);

		if(whileHigh)
		{
			c = not c;	
		}

		if(c)
		{
			//Debug_println("timeoutWait: false");
			return false;
		}

		fnSystem.delay_microseconds(1); // The aim is to make the loop at least 3 us
		t++;
	}
	// If down here, we have had a timeout.
	// Release lines and go to inactive state with error flag
	writeCLOCK(false);
	writeDATA(false);

	m_state = errorFlag;

	// Wait for ATN release, problem might have occured during attention
	//while(not readATN());

	// Note: The while above is without timeout. If ATN is held low forever,
	//       the CBM is out in the woods and needs a reset anyways.

	Debug_printf("\r\ntimeoutWait: true [%d] [%d] [%d] [%d]", waitBit, whileHigh, t, m_state);
	return true;
} // timeoutWait


// IEC Recieve byte standard function
//
// Returns data recieved
// Might set flags in iec_state
//
// FIXME: m_iec might be better returning bool and returning read byte as reference in order to indicate any error.
int IEC::receiveByte(void)
{
	m_state = noFlags;

	// Wait for talker ready
	if(timeoutWait(IEC_PIN_CLOCK, false))
		return 0;

	// Say we're ready
	writeDATA(false);

	// Record how long CLOCK is high, more than 200 us means EOI
	int n = 0;
	while(readCLOCK() and (n < 20)) {
		fnSystem.delay_microseconds(10);  // this loop should cycle in about 10 us...
		n++;
	}

	if(n >= TIMING_EOI_THRESH) {
		// EOI intermission
		m_state or_eq eoiFlag;

		// Acknowledge by pull down data more than 60 us
		writeDATA(true);
		fnSystem.delay_microseconds(TIMING_BIT);
		writeDATA(false);

		// but still wait for clk
		if(timeoutWait(IEC_PIN_CLOCK, true))
			return 0;
	}

	// Sample ATN
	if(false == readATN())
		m_state or_eq atnFlag;

	int data = 0;
	// Get the bits, sampling on clock rising edge:
//	ESP.wdtFeed();
	for(n = 0; n < 8; n++) {
		data >>= 1;
		if(timeoutWait(IEC_PIN_CLOCK, false))
			return 0;
		data or_eq (readDATA() ? (1 << 7) : 0);
		if(timeoutWait(IEC_PIN_CLOCK, true))
			return 0;
	}
	//Debug_printf("%.2X ", data);

	// Signal we accepted data:
	writeDATA(true);

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
	writeCLOCK(false);

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

		writeCLOCK(true);
		// set data
		writeDATA((data bitand 1) ? false : true);

		fnSystem.delay_microseconds(TIMING_BIT);
		writeCLOCK(false);
		fnSystem.delay_microseconds(TIMING_BIT);

		data >>= 1;
	}

	writeCLOCK(true);
	writeDATA(false);

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
// Return value, see IEC::ATNCheck definition.
IEC::ATNCheck IEC::checkATN(ATNCmd& atn_cmd)
{
	ATNCheck ret = ATN_IDLE;
	int i = 0;

	if(not readATN()) {

		// Attention line is active, go to listener mode and get message. Being fast with the next two lines here is CRITICAL!
		writeDATA(true);
		writeCLOCK(false);
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
