// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
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

#ifdef BUILD_IEC

#include "cpbstandardserial.h"

#include "bus.h"
#include "_protocol.h"

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

using namespace Protocol;


/**
 * Callback function to sendbits 
 */
static void onSendBits(void *arg)
{
//    IECProtocol *p = (CPBStandardSerial *)arg;

    uint8_t Tv = TIMING_Tv64; // C64 data valid timing

    // We can send faster if in VIC20 Mode
    if ( IEC.vic20_mode )
    {
        Tv = TIMING_Tv; // VIC-20 data valid timing
    }

    // Send bits
    for ( uint8_t n = 0; n < 8; n++ )
    {
        // set bit
        usleep ( TIMING_Ts0 );
        ( IEC.byte & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
        IEC.byte >>= 1; // shift to next bit
        usleep ( TIMING_Ts1 );

        // tell listener bit is ready to read
        //IEC.pull ( PIN_IEC_SRQ );
        IEC.release ( PIN_IEC_CLK_OUT );
        usleep ( Tv );

        // tell listener to wait for next bit
        IEC.pull ( PIN_IEC_CLK_OUT );
        //IEC.release ( PIN_IEC_SRQ );
        IEC.bit++;
    }
    IEC.bit++;

    // Release DATA after byte sent
    IEC.release ( PIN_IEC_DATA_OUT );
}

CPBStandardSerial::CPBStandardSerial()
{
    const esp_timer_create_args_t args = {
        .callback = onSendBits,
        .arg = this,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "Send"
    };
    esp_timer_create(&args, &timer_send_h);
    //Debug_printv("send_timer_create");
};

CPBStandardSerial::~CPBStandardSerial()
{
    esp_timer_stop(timer_send_h);
    esp_timer_delete(timer_send_h);
    //Debug_printv("send_timer_delete");
}


// STEP 1: READY TO RECEIVE
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and respond,
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's
// a printer chugging out a line of print, or a disk drive with a formatting job in progress,
// it might holdback for quite a while; there's no time limit.
uint8_t CPBStandardSerial::receiveByte()
{
    IEC.flags &= CLEAR_LOW;

    // Wait for talker ready
    //IEC.pull ( PIN_IEC_SRQ );
    if ( timeoutWait ( PIN_IEC_CLK_IN, RELEASED, FOREVER, false ) == TIMED_OUT )
    {
        Debug_printv ( "Wait for talker ready" );
        IEC.flags |= ERROR;
        return 0; // return error because timeout
    }
    //IEC.release ( PIN_IEC_SRQ );

    // Say we're ready
    // STEP 2: READY FOR DATA
    // When  the  listener  is  ready  to  listen,  it  releases  the  Data
    // line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false
    // only when all listeners have RELEASED it - in other words, when  all  listeners  are  ready
    // to  accept  data.  What  happens  next  is  variable.

    // Wait for all other devices to release the data line
    if ( timeoutWait ( PIN_IEC_DATA_IN, RELEASED, FOREVER, false ) == TIMED_OUT )
    {
        Debug_printv ( "Wait for all other devices to release the data line" );
        IEC.flags |= ERROR;
        return 0; // return error because timeout
    }

    // Either  the  talker  will pull the
    // Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it
    // will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass
    // without  the Clock line going to true, it has a special task to perform: note EOI.

    //IEC.pull ( PIN_IEC_SRQ );
    //if ( timeoutWait ( PIN_IEC_CLK_IN, PULLED, TIMING_Tye, false ) == TIMING_Tye )
    timer_start( TIMING_Tye );
    while ( IEC.status ( PIN_IEC_CLK_IN ) != PULLED )
    {
        // INTERMISSION: EOI
        // If the Ready for Data signal isn't acknowledged by the talker within 200 microseconds, the
        // listener knows  that  the  talker  is  trying  to  signal  EOI.    EOI,  which  formally
        // stands  for  "End  of  Indicator," means  "this  character  will  be  the  last  one."
        // If  it's  a  sequential  disk  file,  don't  ask  for  more:  there will be no more.  If it's
        // a relative record, that's the end of the record.  The character itself will still be coming, but
        // the listener should note: here comes the last character. So if the listener sees the 200 microsecond
        // time-out,  it  must  signal  "OK,  I  noticed  the  EOI"  back  to  the  talker,    It  does  this
        // by pulling  the  Data  line  true  for  at  least  60  microseconds,  and  then  releasing  it.
        // The  talker  will  then revert to transmitting the character in the usual way; within 60 microseconds
        // it will pull the Clock line  true,  and  transmission  will  continue.  At  this point,  the  Clock
        // line  is  true  whether  or  not  we have gone through the EOI sequence; we're back to a common
        // transmission sequence.

        IEC.pull ( PIN_IEC_SRQ );

        if ( timer_timedout )
        {
            timer_timedout = false;
            IEC.flags |= EOI_RECVD;

            // Acknowledge by pull down data more than 60us
            wait ( TIMING_Th );
            IEC.pull ( PIN_IEC_DATA_OUT );
            wait ( TIMING_Tei );
            IEC.release ( PIN_IEC_DATA_OUT );
        }

        usleep( 2 );
        IEC.release ( PIN_IEC_SRQ );
        usleep( 2 );
    }
    //IEC.release ( PIN_IEC_SRQ );

    // Sample ATN and set flag to indicate COMMAND or DATA mode
    if ( IEC.status ( PIN_IEC_ATN ) )
        IEC.flags |= ATN_PULLED;

    // STEP 3: RECEIVING THE BITS
    //IEC.pull ( PIN_IEC_SRQ );
    uint8_t data = receiveBits();
    //IEC.release ( PIN_IEC_SRQ );

    // STEP 4: FRAME HANDSHAKE
    // After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true
    // and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data
    // line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within
    // one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

    // Acknowledge byte received
    //IEC.pull ( PIN_IEC_SRQ );
    wait ( TIMING_Tf );
    IEC.pull ( PIN_IEC_DATA_OUT );
    //IEC.release ( PIN_IEC_SRQ );

    // STEP 5: START OVER
    // We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,
    // and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has
    // happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause,
    // the Clock and Data lines are RELEASED to false and transmission stops.

    // Lines will be released when exiting the service loop

    return data;
}


// STEP 3: RECEIVING THE BITS
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

uint8_t CPBStandardSerial::receiveBits ()
{
    IEC.bit = 0;
    IEC.byte = 0;
    timer_start( TIMEOUT_DEFAULT );

    while ( IEC.bit < 7 )
    {
        if ( timer_timedout )
        {
            IEC.flags |= ERROR;
            return 0;
        }

        usleep( 2 );
    }

    // If there is a 218us delay before bit 7, the controller uses JiffyDOS
    timer_start( TIMING_PROTOCOL_DETECT );
    while ( IEC.bit < 8 )
    {
        // Are we in COMMAND mode?
        if ( IEC.flags &  ATN_PULLED )
        {
            // Have we timed out?
            if ( timer_timedout )
            {
                // Check LISTEN & TALK
                uint8_t device = (IEC.byte >> 1) & 0x1F; // LISTEN
                if ( device > 30 )
                    device = (IEC.byte >> 1 ) & 0x3F; // TALK

                if ( IEC.isDeviceEnabled ( device ) )
                {
                    // acknowledge we support JiffyDOS
                    IEC.pull(PIN_IEC_DATA_OUT);
                    wait( TIMING_PROTOCOL_ACK, false );
                    IEC.release(PIN_IEC_DATA_OUT);

                    IEC.flags |= JIFFYDOS_ACTIVE;
                }
                timer_timedout = false;
            }
        }

        usleep( 2 );
    }

    return IEC.byte;
}


// uint8_t CPBStandardSerial::receiveBits ()
// {
//     // Listening for bits
//     uint8_t data = 0;
//     int16_t bit_time = 0;  // Used to detect JiffyDOS

//     //IEC.pull ( PIN_IEC_SRQ );
// #ifndef IEC_SPLIT_LINES
//     IEC.release(PIN_IEC_CLK_IN);
//     IEC.release(PIN_IEC_DATA_IN); // Set DATA IN back to input
// #endif

//     uint8_t n = 0;
//     for ( n = 0; n < 8; n++ )
//     {
//         // Time the release of the clock line to detect JiffyDOS
//         //IEC.pull ( PIN_IEC_SRQ );
//         //bit_time = timeoutWait ( PIN_IEC_CLK_IN, RELEASED, TIMING_PROTOCOL_DETECT, false );
//         while ( IEC.status(PIN_IEC_CLK_IN) != RELEASED );
//         //IEC.pull ( PIN_IEC_SRQ );
//         //IEC.release ( PIN_IEC_SRQ );

// #ifdef JIFFYDOS
//         // If there is a 218us delay before bit 7, the controller uses JiffyDOS
//         if ( bit_time >= TIMING_PROTOCOL_DETECT )
//         {
//             //IEC.pull ( PIN_IEC_SRQ );
//             if ( n == 7 && IEC.status( PIN_IEC_ATN ) )
//             {
//                 // Check LISTEN & TALK
//                 uint8_t device = (data >> 1) & 0x1F; // LISTEN
//                 if ( device > 30 )
//                     device = (data >> 1 ) & 0x3F; // TALK

//                 if ( device < 31 )
//                 {
//                     if ( IEC.isDeviceEnabled ( device ) )
//                     {
//                         // acknowledge we support JiffyDOS
//                         IEC.pull(PIN_IEC_DATA_OUT);
//                         wait( TIMING_PROTOCOL_ACK, false );
//                         IEC.release(PIN_IEC_DATA_OUT);

//                         IEC.flags |= JIFFYDOS_ACTIVE;
//                     }
//                 }
//             }
//             //IEC.release ( PIN_IEC_SRQ );

//             // wait for bit to be ready to read
//             //IEC.pull ( PIN_IEC_SRQ );
//             if ( timeoutWait ( PIN_IEC_CLK_IN, RELEASED ) == TIMED_OUT )
//             {
//                 if ( n == 0 )
//                 {
//                     Debug_printv ( "empty stream signaled" );
//                     IEC.flags |= EMPTY_STREAM;
//                 }
//                 else
//                 {
//                     Debug_printv ( "bit %d timeout", n );
//                     IEC.flags |= ERROR;
//                 }
//                 return data;
//             }
//             //IEC.release ( PIN_IEC_SRQ );
//         }
// #endif

//         // get bit
//         data >>= 1;
//         if ( !IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0x80;
//         //IEC.release ( PIN_IEC_SRQ );

//         // wait for talker to finish sending bit
//         // if ( timeoutWait ( PIN_IEC_CLK_IN, PULLED ) == TIMED_OUT )
//         // {
//         //     Debug_printv ( "wait for talker to finish sending bit n[%d]", n );
//         //     IEC.flags |= ERROR;
//         //     return data; // return error because timeout
//         // }
//         while ( IEC.status(PIN_IEC_CLK_IN) != PULLED );
//         IEC.release ( PIN_IEC_SRQ );
//     }
//     //IEC.release ( PIN_IEC_SRQ );

//     return data;
// }


// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and respond,
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's
// a printer chugging out a line of print, or a disk drive with a formatting job in progress,
// it might holdback for quite a while; there's no time limit.
bool CPBStandardSerial::sendByte(uint8_t data, bool eoi)
{
    //IEC.pull ( PIN_IEC_SRQ );

    IEC.flags &= CLEAR_LOW;

    // Say we're ready
    IEC.release ( PIN_IEC_CLK_OUT );

    // Wait for listener to be ready
    // STEP 2: READY FOR DATA
    // When  the  listener  is  ready  to  listen,  it  releases  the  Data
    // line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false
    // only when all listeners have RELEASED it - in other words, when  all  listeners  are  ready
    // to  accept  data.
    //IEC.pull ( PIN_IEC_SRQ );
    if ( timeoutWait ( PIN_IEC_DATA_IN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        if ( !(IEC.flags & ATN_PULLED) )
        {
            Debug_printv ( "Wait for listener to be ready [%02X]", data );
            IEC.flags |= ERROR;
        }

        return false; // return error because of ATN or timeout
    }
    //IEC.release ( PIN_IEC_SRQ );

    // What  happens  next  is  variable. Either  the  talker  will pull the
    // Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it
    // will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass
    // without  the Clock line going to true, it has a special task to perform: note EOI.
    if ( eoi )
    {
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

        // Wait for EOI ACK
        // This will happen after appx 250us
        if ( timeoutWait ( PIN_IEC_DATA_IN, PULLED ) == TIMED_OUT )
        {
            Debug_printv ( "EOI ACK: Listener didn't PULL DATA [%02X]", data );
            IEC.flags |= ERROR;
            return false; // return error because timeout
        }

        // Sender ACK?
        // 1541 release CLK in the middle of the EOI ACK
        usleep ( TIMING_Tpr );
        IEC.pull ( PIN_IEC_CLK_OUT );

        if ( timeoutWait ( PIN_IEC_DATA_IN, RELEASED ) == TIMED_OUT )
        {
            Debug_printv ( "EOI ACK: Listener didn't RELEASE DATA [%02X]", data );
            IEC.flags |= ERROR;
            return false; // return error because timeout
        }
    }

    // *** IMPORTANT!!!
    // Delay before sending bits
    // ATN might get pulled here
    if ( !wait ( TIMING_Tne, true ) ) return false;
    IEC.pull ( PIN_IEC_CLK_OUT );

    // STEP 3: SENDING THE BITS
    //IEC.pull ( PIN_IEC_SRQ );
    if ( !sendBits( data ) ) {
        Debug_printv ( "Error sending bits - byte '%02X'", data );
        IEC.flags |= ERROR;
        return false;
    }
    //IEC.release ( PIN_IEC_SRQ );

    // STEP 4: FRAME HANDSHAKE
    // After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true
    // and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data
    // line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within
    // one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

    // Wait for listener to accept data
    //IEC.pull ( PIN_IEC_SRQ );
    if ( timeoutWait ( PIN_IEC_DATA_IN, PULLED, TIMEOUT_Tf ) >= TIMEOUT_Tf )
    {
        // RECIEVER TIMEOUT
        // If no receiver pulls DATA within 1000 µs at the end of the transmission of a byte (after step 28), a receiver timeout is raised.
        Debug_printv ( "Wait for listener to acknowledge byte received (pull data) [%02x]", data );
        Debug_printv ( "RECEIVER TIMEOUT" );
        IEC.flags |= ERROR;
        return false; // return error because timeout
    }
    //IEC.release ( PIN_IEC_SRQ );

    // STEP 5: START OVER
    // We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,
    // and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has
    // happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause,
    // the Clock and Data lines are RELEASED to false and transmission stops.

    // Lines will be released when exiting the service loop

    return true;
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


// bool CPBStandardSerial::sendBits ( uint8_t data )
// {
// //    uint8_t b = 255;
// //    timer_start( TIMEOUT_DEFAULT );
// //    IEC.pull ( PIN_IEC_SRQ );
//     IEC.bit = 0;
//     IEC.byte = data;

//     esp_timer_start_once(timer_send_h, 5);
//     while ( IEC.bit < 8 )
//     {
//         // if ( timer_timedout )
//         // {
//         //     IEC.flags |= ERROR;
//         //     return false;
//         // }

//         usleep( 10 );
//     }

//     // IEC.release ( PIN_IEC_SRQ );
//     esp_intr_enable_source( PIN_IEC_CLK_IN );
//     return true;
// }

bool CPBStandardSerial::sendBits ( uint8_t data )
{
    uint8_t Tv = TIMING_Tv64; // C64 data valid timing

    // We can send faster if in VIC20 Mode
    if ( IEC.vic20_mode )
    {
        Tv = TIMING_Tv; // VIC-20 data valid timing
    }

    // Send bits
    for ( uint8_t n = 0; n < 8; n++ )
    {
        // set bit
        usleep ( TIMING_Ts0 );
        ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
        data >>= 1; // shift to next bit
        usleep ( TIMING_Ts1 );

        // tell listener bit is ready to read
        IEC.release ( PIN_IEC_CLK_OUT );
        usleep ( Tv );

        // tell listener to wait for next bit
        IEC.pull ( PIN_IEC_CLK_OUT );
    }

    // Release DATA after byte sent
    IEC.release ( PIN_IEC_DATA_OUT );

    return true;
} // sendBits

#endif // BUILD_IEC