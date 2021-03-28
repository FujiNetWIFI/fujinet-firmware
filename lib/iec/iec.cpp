#include "../../include/debug.h"

#include "iec.h"

using namespace CBM;

IEC::IEC() : m_state(noFlags)
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
	pull(IEC_PIN_ATN);
	pull(IEC_PIN_CLK);
	pull(IEC_PIN_DATA);
	pull(IEC_PIN_SRQ);

	// TODO:
	//#ifdef RESET_C64
	//	release(IEC_PIN_RESET);	// only early C64's could be reset by a slave going high.
	//#endif

	// initial pin modes in GPIO
	set_pin_mode(IEC_PIN_ATN, gpio_mode_t::GPIO_MODE_INPUT);
	set_pin_mode(IEC_PIN_CLK, gpio_mode_t::GPIO_MODE_INPUT);
	set_pin_mode(IEC_PIN_DATA, gpio_mode_t::GPIO_MODE_INPUT);
	set_pin_mode(IEC_PIN_SRQ, gpio_mode_t::GPIO_MODE_INPUT);

	m_state = noFlags;

	return true;
} // init

// timeoutWait returns true if timed out
bool IEC::timeoutWait(int iecPIN, IECline lineStatus)
{
	uint16_t t = 0;
	IECline c;

	while(t < TIMEOUT) {

		// Check the waiting condition:
		if(status(iecPIN) == lineStatus)
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

	m_state = errorFlag;

	// Wait for ATN release, problem might have occured during attention
	while(status(IEC_PIN_ATN) == pulled);

	// Note: The while above is without timeout. If ATN is held low forever,
	//       the CBM is out in the woods and needs a reset anyways.

	Debug_printf("\r\ntimeoutWait: true [%d] [%d] [%d] [%d]", iecPIN, lineStatus, t, m_state);
	return true;
} // timeoutWait


// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a character. 
// When it's ready to go, it releases the Clock line to false.  This signal change might be 
// translated as "I'm ready to send a character." The listener must detect this and respond, 
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's  
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's  
// a printer chugging out a line of print, or a disk drive with a formatting job in progress, 
// it might holdback for quite a while; there's no time limit. 
int IEC::receiveByte(void)
{
	m_state = noFlags;

	// Wait for talker ready
	if (timeoutWait(IEC_PIN_CLK, released))
		return -1; // return error because timeout

	// Say we're ready
	// STEP 2: READY FOR DATA
	// When  the  listener  is  ready  to  listen,  it  releases  the  Data  
	// line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false 
	// only when all listeners have released it - in other words, when  all  listeners  are  ready  
	// to  accept  data.  What  happens  next  is  variable.     
	release(IEC_PIN_DATA);

	// Either  the  talker  will pull the 
	// Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it  
	// will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass  
	// without  the Clock line going to true, it has a special task to perform: note EOI.
	int n = 0;
	while ((status(IEC_PIN_CLK) == released) and (n < 20))
	{
		fnSystem.delay_microseconds(1); // this loop should cycle in about 10 us...
		n++;
	}

	if (n >= TIMING_EOI_THRESH)
	{

		// INTERMISSION: EOI
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

		m_state or_eq eoiFlag; // or_eq, |=

		// Acknowledge by pull down data more than 60 us
		pull(IEC_PIN_DATA);
		fnSystem.delay_microseconds(TIMING_BIT);
		release(IEC_PIN_DATA);

		// C64 should pull clock in response
		if (timeoutWait(IEC_PIN_CLK, pulled))
			return -1;
	}

	// Sample ATN and set flag to indicate SELECT or DATA mode
	if(status(IEC_PIN_ATN) == pulled)
		m_state or_eq atnFlag;


	// STEP 3: SENDING THE BITS
	// The talker has eight bits to send.  They will go out without handshake; in other words, 
	// the listener had better be there to catch them, since the talker won't wait to hear from the listener.  At this 
	// point, the talker controls both lines, Clock and Data.  At the beginning of the sequence, it is holding the 
	// Clock true, while the Data line is released to false.  the Data line will change soon, since we'll sendthe data 
	// over it. The eights bits will go out from the character one at a time, with the least significant bit going first.  
	// For example, if the character is the ASCII question mark, which is  written  in  binary  as  00011111,  the  ones  
	// will  go out  first,  followed  by  the  zeros.  Now,  for  each bit, we set the Data line true or false according 
	// to whether the bit is one or zero.  As soon as that'sset, the Clock line is released to false, signalling "data ready."  
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
	for (n = 0; n < 8; n++)
	{
		data >>= 1;

		// wait for bit to be ready to read
		if (timeoutWait(IEC_PIN_CLK, released)) // look for rising edge
			return -1;
		
		// get bit
		data or_eq (status(IEC_PIN_DATA) == released ? (1 << 7) : 0); // read bit and shift in LSB first

		// wait for talker to finish sending bit
		if (timeoutWait(IEC_PIN_CLK, pulled))			// wait for falling edge
			return -1;
	}
	//Debug_printf("%.2X ", data);

	// STEP 4: FRAME HANDSHAKE
	// After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true  
	// and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data 
	// line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within  
	// one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

	// Acknowledge byte received
	pull(IEC_PIN_DATA);

	// STEP 5: START OVER
	// We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,  
	// and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has 
	// happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause, 
	// the Clock and Data lines are released to false and transmission stops. 

	// if(m_state bitand eoiFlag)
	// {
	// 	// EOI Received
	// 	delayMicroseconds(TIMING_STABLE_WAIT);
	// 	//release(IEC_PIN_CLK);
	// 	release(IEC_PIN_DATA);
	// }

	return data;
} // receiveByte


// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a character. 
// When it's ready to go, it releases the Clock line to false.  This signal change might be 
// translated as "I'm ready to send a character." The listener must detect this and respond, 
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's  
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's  
// a printer chugging out a line of print, or a disk drive with a formatting job in progress, 
// it might holdback for quite a while; there's no time limit. 
bool IEC::sendByte(int data, bool signalEOI)
{
	//m_state = noFlags;

	// Say we're ready
	release(IEC_PIN_CLK);

	// Wait for listener to be ready
	// STEP 2: READY FOR DATA
	// When  the  listener  is  ready  to  listen,  it  releases  the  Data  
	// line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false 
	// only when all listeners have released it - in other words, when  all  listeners  are  ready  
	// to  accept  data.  What  happens  next  is  variable. 
	if (timeoutWait(IEC_PIN_DATA, released))
		return false;

	// Either  the  talker  will pull the 
	// Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it  
	// will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass  
	// without  the Clock line going to true, it has a special task to perform: note EOI.
	if (signalEOI)
	{
		// INTERMISSION: EOI
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

		//m_state or_eq eoiFlag;

		// Signal eoi by waiting 200 us
		fnSystem.delay_microseconds(TIMING_EOI_WAIT);

		// get eoi acknowledge: pull
		if (timeoutWait(IEC_PIN_DATA, pulled))
			return false;

		// get eoi acknowledge: release
		if (timeoutWait(IEC_PIN_DATA, released))
			return false;
	}
	else
	{
		fnSystem.delay_microseconds(TIMING_NO_EOI);		
	}

	// STEP 3: SENDING THE BITS
	// The talker has eight bits to send.  They will go out without handshake; in other words, 
	// the listener had better be there to catch them, since the talker won't wait to hear from the listener.  At this 
	// point, the talker controls both lines, Clock and Data.  At the beginning of the sequence, it is holding the 
	// Clock true, while the Data line is released to false.  the Data line will change soon, since we'll sendthe data 
	// over it. The eights bits will go out from the character one at a time, with the least significant bit going first.  
	// For example, if the character is the ASCII question mark, which is  written  in  binary  as  00011111,  the  ones  
	// will  go out  first,  followed  by  the  zeros.  Now,  for  each bit, we set the Data line true or false according 
	// to whether the bit is one or zero.  As soon as that'sset, the Clock line is released to false, signalling "data ready."  
	// The talker will typically have a bit in  place  and  be  signalling  ready  in  70  microseconds  or  less.  Once  
	// the  talker  has  signalled  "data ready," it will hold the two lines steady for at least 20 microseconds timing needs 
	// to be increased to 60  microseconds  if  the  Commodore  64  is  listening,  since  the  64's  video  chip  may  
	// interrupt  the processor for 42 microseconds at a time, and without the extra wait the 64 might completely miss a 
	// bit. The listener plays a passive role here; it sends nothing, and just watches.  As soon as it sees the Clock line 
	// false, it grabs the bit from the Data line and puts it away.  It then waits for the clock line to go true, in order 
	// to prepare for the next bit. When the talker figures the data has been held for a sufficient  length  of  time,  it  
	// pulls  the  Clock  line true  and  releases  the  Data  line  to  false.    Then  it starts to prepare the next bit.

	// Send bits
	for (int n = 0; n < 8; n++)
	{
		// FIXME: Here check whether data pin goes low, if so end (enter cleanup)!

		// tell listner to wait
		pull(IEC_PIN_CLK);							 // pull clock low

		// set data bit
		(data bitand 1) ? pull(IEC_PIN_DATA) : release(IEC_PIN_DATA); // set data
		fnSystem.delay_microseconds(TIMING_BIT);	 // hold data

		// tell listener bit is ready to read
		release(IEC_PIN_CLK);						 // rising edge
		fnSystem.delay_microseconds(TIMING_BIT);	 // hold data

		data >>= 1; // get next bit
	}

	pull(IEC_PIN_CLK);	// pull clock cause we're done
	release(IEC_PIN_DATA); // release data because we're done

	// STEP 4: FRAME HANDSHAKE
	// After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true  
	// and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data 
	// line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within  
	// one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

	// Wait for listener to accept data
	if (timeoutWait(IEC_PIN_DATA, pulled))
		return false;

	// STEP 5: START OVER
	// We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,  
	// and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has 
	// happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause, 
	// the Clock and Data lines are released to false and transmission stops. 

//	if(m_state bitand eoiFlag)
//	{
//		// EOI Received
//		delayMicroseconds(TIMING_STABLE_WAIT);
//		release(IEC_PIN_CLK);
//		release(IEC_PIN_DATA);
//	}

	return true;
} // sendByte

// IEC turnaround
bool IEC::turnAround(void)
{
	/*
	TURNAROUND
	An unusual sequence takes place following ATN if the computer wishes the remote device to
	become a talker. This will usually take place only after a Talk command has been sent.
	Immediately after ATN is released, the selected device will be behaving like a listener. After all, it's
	been listening during the ATN cycle, and the computer
	has been a talker. At this instant, we have "wrong way" logic; the device is holding down the Data
	line, and the computer is holding the Clock line. We must turn this around. Here's the sequence:
	the computer quickly realizes what's going on, and pulls the Data line to true (it's already there), as
	well as releasing the Clock line to false. The device waits for this: when it sees the Clock line go
	true [sic], it releases the Data line (which stays true anyway since the computer is now holding it down)
	and then pulls down the Clock line. We're now in our starting position, with the talker (that's the
	device) holding the Clock true, and the listener (the computer) holding the Data line true. The
	computer watches for this state; only when it has gone through the cycle correctly will it be ready
	to receive data. And data will be signalled, of course, with the usual sequence: the talker releases
	the Clock line to signal that it's ready to send.
	*/
	Debug_printf("\r\nturnAround: ");

	// Wait until clock is released
	if (timeoutWait(IEC_PIN_CLK, released))
	{
		Debug_println("timeout");
		return false;
	}

	release(IEC_PIN_DATA);
	fnSystem.delay_microseconds(TIMING_BIT);
	pull(IEC_PIN_CLK);
	fnSystem.delay_microseconds(TIMING_BIT);

	Debug_println("complete");
	return true;
} // turnAround

// this routine will set the direction on the bus back to normal
// (the way it was when the computer was switched on)
bool IEC::undoTurnAround(void)
{
	pull(IEC_PIN_DATA);
	fnSystem.delay_microseconds(TIMING_BIT);
	release(IEC_PIN_CLK);
	fnSystem.delay_microseconds(TIMING_BIT);

	Debug_printf("\r\nundoTurnAround:");

	// wait until the computer pulls the clock line
	if (timeoutWait(IEC_PIN_CLK, pulled))
	{
		Debug_print("timeout");
		return false;
	}

	Debug_print("complete");
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
IEC::ATNCheck IEC::checkATN(ATNCmd &atn_cmd)
{
	ATNCheck ret = ATN_IDLE;
	int i = 0;

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
		if (m_state bitand errorFlag)
		{
			Debug_printf("\r\nm_state bitand errorFlag 0");
			return ATN_ERROR;
		}

		atn_cmd.code = c;

		ATNCommand cc = c;
		if (c != ATN_CODE_UNTALK && c != ATN_CODE_UNLISTEN)
		{
			// Is this a Listen or Talk command
			cc = (ATNCommand)(c bitand ATN_CODE_LISTEN);
			if (cc == ATN_CODE_LISTEN)
			{
				atn_cmd.device = c ^ ATN_CODE_LISTEN; // device specified, '^' = XOR
			}
			else
			{
				cc = (ATNCommand)(c bitand ATN_CODE_TALK);
				atn_cmd.device = c ^ ATN_CODE_TALK; // device specified
			}

			// Get the first cmd byte, the atn_cmd.code
			c = (ATNCommand)receive();
			if (m_state bitand errorFlag)
			{
				Debug_printf("\r\nm_state bitand errorFlag 1");
				return ATN_ERROR;
			}

			atn_cmd.code = c;
			atn_cmd.command = c bitand 0xF0; // upper nibble, the command itself
			atn_cmd.channel = c bitand 0x0F; // lower nibble is the channel
		}

		if (cc == ATN_CODE_LISTEN && isDeviceEnabled(atn_cmd.device))
		{
			ret = deviceListen(atn_cmd);
		}
		else if (cc == ATN_CODE_TALK && isDeviceEnabled(atn_cmd.device))
		{
			ret = deviceTalk(atn_cmd);
		}
		else
		{
			// Either the message is not for us or insignificant, like unlisten.
			fnSystem.delay_microseconds(TIMING_ATN_DELAY);
			release(IEC_PIN_DATA);
			release(IEC_PIN_CLK);

			if (cc == ATN_CODE_UNTALK)
				Debug_print("UNTALK");
			if (cc == ATN_CODE_UNLISTEN)
				Debug_print("UNLISTEN");

			Debug_printf(" (%.2d DEVICE)", atn_cmd.device);

			// Wait for ATN to release and quit
			//while(not status(IEC_PIN_ATN));
			while(status(IEC_PIN_ATN) == pulled);
			Debug_printf("\r\ncheckATN: ATN Released\r\n");
		}

		// some delay is required before more ATN business can take place.
		fnSystem.delay_microseconds(TIMING_ATN_DELAY);

		atn_cmd.strLen = i;
	}
	else
	{
		// No ATN, keep lines in a released state.
		release(IEC_PIN_DATA);
		release(IEC_PIN_CLK);
	}

	return ret;
} // checkATN

IEC::ATNCheck IEC::deviceListen(ATNCmd &atn_cmd)
{
	int i = 0;
	ATNCommand c;

	// Okay, we will listen.
	Debug_printf("(20 LISTEN) (%.2d DEVICE)", atn_cmd.device);

	// If the command is DATA and it is not to expect just a small command on the command channel, then
	// we're into something more heavy. Otherwise read it all out right here until UNLISTEN is received.
	if (atn_cmd.command == ATN_CODE_DATA and atn_cmd.channel not_eq CMD_CHANNEL)
	{
		// A heapload of data might come now, too big for this context to handle so the caller handles this, we're done here.
		Debug_printf("\r\ncheckATN: %.2X (DATA)      (%.2X COMMAND) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);
		return ATN_CMD_LISTEN;
	}
	else if (atn_cmd.command not_eq ATN_CODE_UNLISTEN)
	//if(c not_eq ATN_CODE_UNLISTEN)
	{

		if (atn_cmd.command == ATN_CODE_OPEN)
		{
			Debug_printf("\r\ncheckATN: %.2X (%.2X OPEN) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);
		}
		else if (atn_cmd.command == ATN_CODE_CLOSE)
		{
			Debug_printf("\r\ncheckATN: %.2X (%.2X CLOSE) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);
		}

		// Some other command. Record the cmd string until UNLISTEN is sent
		for (;;)
		{
			c = (ATNCommand)receive();
			if (m_state bitand errorFlag)
			{
				Debug_printf("\r\nm_state bitand errorFlag 2");
				return ATN_ERROR;
			}

			if ((m_state bitand atnFlag) and (ATN_CODE_UNLISTEN == c))
			{
				Debug_printf(" [%s]", atn_cmd.str);
				Debug_printf("\r\ncheckATN: %.2X (UNLISTEN)", c);
				break;
			}

			if (i >= ATN_CMD_MAX_LENGTH)
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

// IEC::ATNCheck IEC::deviceUnListen(ATNCmd& atn_cmd)
// {

// }

IEC::ATNCheck IEC::deviceTalk(ATNCmd &atn_cmd)
{
	int i = 0;
	ATNCommand c;

	// Okay, we will talk soon
	Debug_printf("(40 TALK) (%.2d DEVICE)", atn_cmd.device);
	Debug_printf("\r\ncheckATN: %.2X (%.2X SECOND) (%.2X CHANNEL)", atn_cmd.code, atn_cmd.command, atn_cmd.channel);

	while (status(IEC_PIN_ATN) == released)
	{
		if (status(IEC_PIN_CLK) == pulled)
		{
			c = (ATNCommand)receive();
			if (m_state bitand errorFlag)
				return ATN_ERROR;

			if (i >= ATN_CMD_MAX_LENGTH)
			{
				// Buffer is going to overflow, this is an error condition
				// FIXME: here we should propagate the error type being overflow so that reading error channel can give right code out.
				return ATN_ERROR;
			}
			atn_cmd.str[i++] = c;
			atn_cmd.str[i] = '\0';
		}
	}

	// Now ATN has just been released, do bus turnaround
	if (not turnAround())
		return ATN_ERROR;

	// We have recieved a CMD and we should talk now:
	return ATN_CMD_TALK;
}

// IEC::ATNCheck IEC::deviceUnTalk(ATNCmd& atn_cmd)
// {

// }

// bool IEC::checkRESET()
// {
// 	//	return false;
// 	//	// hmmm. Is this all todo?
// 	return readRESET();
// } // checkRESET

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
	if (sendByte(data, true))
	{
		//Debug_print("true");

		// As we have just send last byte, turn bus back around
		if (undoTurnAround())
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
	release(IEC_PIN_DATA);
	release(IEC_PIN_CLK);

	// Hold back a little...
	fnSystem.delay_microseconds(TIMING_FNF_DELAY);

	Debug_printf("\r\nsendFNF: true");
	return true;
} // sendFNF

bool IEC::isDeviceEnabled(const int deviceNumber)
{
	return (enabledDevices & (1 << deviceNumber));
} // isDeviceEnabled

IEC::IECState IEC::state() const
{
	return static_cast<IECState>(m_state);
} // state

IEC iec;
