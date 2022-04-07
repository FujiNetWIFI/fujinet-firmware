#ifdef BUILD_CBM

#include "cbmstandardserial.h"

#include <rom/ets_sys.h>

#include "../../../../include/debug.h"
#include "../../../../include/pinmap.h"

#include "fnSystem.h"

#define delayMicroseconds ets_delay_us

using namespace Protocol;

// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a character. 
// When it's ready to go, it releases the Clock line to false.  This signal change might be 
// translated as "I'm ready to send a character." The listener must detect this and respond, 
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's  
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's  
// a printer chugging out a line of print, or a disk drive with a formatting job in progress, 
// it might holdback for quite a while; there's no time limit. 
int16_t  CBMStandardSerial::receiveByte(uint8_t device)
{
	flags = CLEAR;

	// Wait for talker ready
	while(status(PIN_IEC_CLK) != RELEASED)
	{
#if defined(ESP8266)
		ESP.wdtFeed();
#endif
	}

	// Say we're ready
	// STEP 2: READY FOR DATA
	// When  the  listener  is  ready  to  listen,  it  releases  the  Data  
	// line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false 
	// only when all listeners have RELEASED it - in other words, when  all  listeners  are  ready  
	// to  accept  data.  What  happens  next  is  variable.
	release(PIN_IEC_DATA);
	while(status(PIN_IEC_DATA) != RELEASED); // Wait for all other devices to release the data line
	//timeoutWait(PIN_IEC_DATA, RELEASED, FOREVER, 1);

	// Either  the  talker  will pull the 
	// Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it  
	// will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass  
	// without  the Clock line going to true, it has a special task to perform: note EOI.
	
	if(timeoutWait(PIN_IEC_CLK, PULLED, TIMEOUT_Tne) == TIMED_OUT)
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

		flags or_eq EOI_RECVD;

		// Acknowledge by pull down data more than 60us
		pull(PIN_IEC_DATA);
		delayMicroseconds(TIMING_Tei);
		release(PIN_IEC_DATA);

		// but still wait for CLK to be PULLED
		if(timeoutWait(PIN_IEC_CLK, PULLED) == TIMED_OUT)
		{
			Debug_printf("After Acknowledge EOI");
			flags or_eq ERROR;
			return -1; // return error because timeout
		}		
	}

	// Sample ATN and set flag to indicate SELECT or DATA mode
	if(status(PIN_IEC_ATN) == PULLED)
		flags or_eq ATN_PULLED;

	// STEP 3: SENDING THE BITS
	// The talker has eight bits to send.  They will go out without handshake; in other words, 
	// the listener had better be there to catch them, since the talker won't wait to hear from the listener.  At this 
	// point, the talker controls both lines, Clock and Data.  At the beginning of the sequence, it is holding the 
	// Clock true, while the Data line is RELEASED to false.  the Data line will change soon, since we'll sendthe data 
	// over it. The eights bits will go out from the character one at a time, with the least significant bit going first.  
	// For example, if the character is the ASCII question mark, which is  written  in  binary  as  00011111,  the  ones  
	// will  go out  first,  followed  by  the  zeros.  Now,  for  each bit, we set the Data line true or false according 
	// to whether the bit is one or zero.  As soon as that'sset, the Clock line is RELEASED to false, signalling "data ready."  
	// The talker will typically have a bit in  place  and  be  signalling  ready  in  70  microseconds  or  less.  Once  
	// the  talker  has  signalled  "data ready," it will hold the two lines steady for at least 20 microseconds timing needs 
	// to be increased to 60  microseconds  if  the  Commodore  64  is  listening,  since  the  64's  video  chip  may  
	// interrupt  the processor for 42 microseconds at a time, and without the extra wait the 64 might completely miss a 
	// bit. The listener plays a passive role here; it sends nothing, and just watches.  As soon as it sees the Clock line 
	// false, it grabs the bit from the Data line and puts it away.  It then waits for the clock line to go true, in order 
	// to prepare for the next bit. When the talker figures the data has been held for a sufficient  length  of  time,  it  
	// pulls  the  Clock  line true  and  releases  the  Data  line  to  false.    Then  it starts to prepare the next bit.

	// Listening for bits
#if defined(ESP8266)
	ESP.wdtFeed();
#endif
	uint8_t data = 0;
	int16_t bit_time;  // Used to detect JiffyDOS
	
	uint8_t n = 0;
	for(n = 0; n < 8; n++) 
	{
		data >>= 1;

		// wait for bit to be ready to read
		if(timeoutWait(PIN_IEC_CLK, RELEASED) == TIMED_OUT)
		{
			Debug_printf("wait for bit to be ready to read");
			flags or_eq ERROR;
			return -1; // return error because timeout
		}

		// get bit
		data or_eq (status(PIN_IEC_DATA) == RELEASED ? (1 << 7) : 0);

		// wait for talker to finish sending bit
		bit_time = timeoutWait(PIN_IEC_CLK, PULLED);
		if(bit_time == TIMED_OUT)
		{
			Debug_printf("wait for talker to finish sending bit");
			flags or_eq ERROR;
			return -1; // return error because timeout
		}
	}

	/* If there is a delay before the last bit, the controller uses JiffyDOS */
	if (flags bitand ATN_PULLED && bit_time >= 218 && n == 7) {
		if ((data>>1) < 0x60 && ((data>>1) & 0x1f) == device) {
			/* If it's for us, notify controller that we support Jiffy too */
			// pull(PIN_IEC_DATA);
			// delayMicroseconds(101); // nlq says 405us, but the code shows only 101
			// release(PIN_IEC_DATA);
			flags xor_eq JIFFY_ACTIVE;
		}
	}

	// STEP 4: FRAME HANDSHAKE
	// After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true  
	// and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data 
	// line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within  
	// one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

	// Acknowledge byte received
	delayMicroseconds(TIMING_Tf);
	pull(PIN_IEC_DATA);

	// STEP 5: START OVER
	// We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,  
	// and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has 
	// happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause, 
	// the Clock and Data lines are RELEASED to false and transmission stops. 

	// if(flags bitand EOI_RECVD)
	// {
	// 	// EOI Received
	// 	// delayMicroseconds(TIMING_Tfr);
	// 	// release(PIN_IEC_CLK);
	// 	// release(PIN_IEC_DATA);
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
bool CBMStandardSerial::sendByte(uint8_t data, bool signalEOI)
{
	flags = CLEAR;

	// Say we're ready
	release(PIN_IEC_CLK);

	// Wait for listener to be ready
	// STEP 2: READY FOR DATA
	// When  the  listener  is  ready  to  listen,  it  releases  the  Data  
	// line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false 
	// only when all listeners have RELEASED it - in other words, when  all  listeners  are  ready  
	// to  accept  data.  What  happens  next  is  variable.
	while(status(PIN_IEC_DATA) != RELEASED)
	{
#if defined(ESP8266)
		ESP.wdtFeed();
#endif
	}

	// Either  the  talker  will pull the 
	// Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it  
	// will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass  
	// without  the Clock line going to true, it has a special task to perform: note EOI.
	if(signalEOI) {

		// INTERMISSION: EOI
		// If the Ready for Data signal isn't acknowledged by the talker within 200 microseconds, the 
		// listener knows  that  the  talker  is  trying  to  signal  EOI.    EOI,  which  formally  
		// stands  for  "End  of  Indicator," means  "this  character  will  be  the  last  one."    
		// If  it's  a  sequential  disk  file,  don't  ask  for  more:  there will be no more.  If it's 
		// a relative record, that's the end of the record.  The character itself will still be coming, but 
		// the listener should note: here comes the last character. So if the listener sees the 200 microsecond  
		// time-out,  it  must  signal  "OK,  I  noticed  the  EOI"  back  to  the  talker,  It  does  this  
		// by pulling  the  Data  line  true  for  at  least  60  microseconds,  and  then  releasing  it.  
		// The  talker  will  then revert to transmitting the character in the usual way; within 60 microseconds 
		// it will pull the Clock line  true,  and  transmission  will  continue.  At  this point,  the  Clock  
		// line  is  true  whether  or  not  we have gone through the EOI sequence; we're back to a common 
		// transmission sequence.

		//flags or_eq EOI_RECVD;

		// Signal eoi by waiting 200 us
		delayMicroseconds(TIMING_Tye);

		// get eoi acknowledge:
		if(timeoutWait(PIN_IEC_DATA, PULLED) == TIMED_OUT)
		{
			Debug_printf("Get EOI acknowledge");
			flags or_eq ERROR;
			return false; // return error because timeout
		}

		if(timeoutWait(PIN_IEC_DATA, RELEASED) == TIMED_OUT)
		{
			Debug_printf("Listener didn't release DATA");
			flags or_eq ERROR;
			return false; // return error because timeout
		}
	}
	else
	{
		delayMicroseconds(TIMING_Tne);		
	}

	// STEP 3: SENDING THE BITS
	// The talker has eight bits to send.  They will go out without handshake; in other words, 
	// the listener had better be there to catch them, since the talker won't wait to hear from the listener.  At this 
	// point, the talker controls both lines, Clock and Data.  At the beginning of the sequence, it is holding the 
	// Clock true, while the Data line is RELEASED to false.  the Data line will change soon, since we'll sendthe data 
	// over it. The eights bits will go out from the character one at a time, with the least significant bit going first.  
	// For example, if the character is the ASCII question mark, which is  written  in  binary  as  00011111,  the  ones  
	// will  go out  first,  followed  by  the  zeros.  Now,  for  each bit, we set the Data line true or false according 
	// to whether the bit is one or zero.  As soon as that'sset, the Clock line is RELEASED to false, signalling "data ready."  
	// The talker will typically have a bit in  place  and  be  signalling  ready  in  70  microseconds  or  less.  Once  
	// the  talker  has  signalled  "data ready," it will hold the two lines steady for at least 20 microseconds timing needs 
	// to be increased to 60  microseconds  if  the  Commodore  64  is  listening,  since  the  64's  video  chip  may  
	// interrupt  the processor for 42 microseconds at a time, and without the extra wait the 64 might completely miss a 
	// bit. The listener plays a passive role here; it sends nothing, and just watches.  As soon as it sees the Clock line 
	// false, it grabs the bit from the Data line and puts it away.  It then waits for the clock line to go true, in order 
	// to prepare for the next bit. When the talker figures the data has been held for a sufficient  length  of  time,  it  
	// pulls  the  Clock  line true  and  releases  the  Data  line  to  false.    Then  it starts to prepare the next bit.

	// Send bits
#if defined(ESP8266)
	ESP.wdtFeed();
#endif

	// tell listner to wait
	pull(PIN_IEC_CLK);
	delayMicroseconds(TIMING_Tv);

	for(uint8_t n = 0; n < 8; n++) 
	{
		// FIXME: Here check whether data pin is PULLED, if so end (enter cleanup)!


		// set bit
		(data bitand 1) ? release(PIN_IEC_DATA) : pull(PIN_IEC_DATA);
		delayMicroseconds(TIMING_Tv);

		// tell listener bit is ready to read
		release(PIN_IEC_CLK);
		delayMicroseconds(TIMING_Tv);

		// if ATN is PULLED, exit and cleanup
		if(status(PIN_IEC_ATN) == PULLED)
		{	
			flags or_eq ATN_PULLED;
			return false;
		}

		pull(PIN_IEC_CLK);
		delayMicroseconds(TIMING_Tv);

		data >>= 1; // get next bit		
	}

	// STEP 4: FRAME HANDSHAKE
	// After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true  
	// and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data 
	// line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within  
	// one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

	// Wait for listener to accept data
	if(timeoutWait(PIN_IEC_DATA, PULLED, TIMEOUT_Tf) == TIMED_OUT)
	{
		Debug_printf("Wait for listener to acknowledge byte received");
		return false; // return error because timeout
	}

	// BETWEEN BYTES TIME
	delayMicroseconds(TIMING_Tbb);

	// STEP 5: START OVER
	// We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,  
	// and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has 
	// happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause, 
	// the Clock and Data lines are RELEASED to false and transmission stops. 

	// if(signalEOI)
	// {
	// 	// EOI Received
	// 	delayMicroseconds(TIMING_Tfr);
	// 	release(PIN_IEC_CLK);
	// 	// release(PIN_IEC_DATA);
	// }

	return true;
} // sendByte


// Wait indefinitely if wait = 0
int16_t CBMStandardSerial::timeoutWait(uint8_t iecPIN, bool lineStatus, size_t wait, size_t step)
{

#if defined(ESP8266)
	ESP.wdtFeed();
#endif

	size_t t = 0;
	if(wait == FOREVER)
	{
		while(status(iecPIN) != lineStatus) {
			ESP.wdtFeed();
			delayMicroseconds(step);
			t++;	
		}
		return (t * step);
	}
	else
	{
		while(status(iecPIN) != lineStatus && (t < wait)) {
			delayMicroseconds(step);
			t++;	
		}
		// Check the waiting condition:
		if(t < wait)
		{
			// Got it!  Continue!
			return (t * step);
		}		
	}

	Debug_printf("pin[%d] state[%d] wait[%d] step[%d] t[%d]", iecPIN, lineStatus, wait, step, t);
	return -1;
} // timeoutWait

#endif /* BUILD_CBM */