#ifdef BUILD_IEC

#include "bus.h"
#include "iecProtocolBase.h"
#include "iecProtocolSerial.h"
#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

IecProtocolSerial::IecProtocolSerial()
{

}

IecProtocolSerial::~IecProtocolSerial()
{

}


// ******************************  send data
// E909   78         SEI
// E90A   20 EB D0   JSR $D0EB     open channel for read
// E90D   B0 06      BCS $E915     channel active
// E90F   A6 82      LDX $82       channel number
// E911   B5 F2      LDA $F2,X     set READ flag?
// E913   30 01      BMI $E916     yes
// E915   60         RTS
// E916   20 59 EA   JSR $EA59     check EOI
// E919   20 C0 E9   JSR $E9C0     read IEEE port
// E91C   29 01      AND #$01      isolate data bit
// E91E   08         PHP           and save
// E91F   20 B7 E9   JSR $E9B7     CLOCK OUT lo (PULLED)
// E922   28         PLP
// E923   F0 12      BEQ $E937
// E925   20 59 EA   JSR $EA59     check EOI
// E928   20 C0 E9   JSR $E9C0     read IEEE port
// E92B   29 01      AND #$01      isolate data bit
// E92D   D0 F6      BNE $E925
// E92F   A6 82      LDX $82       channel number
// E931   B5 F2      LDA $F2,X
// E933   29 08      AND #$08
// E935   D0 14      BNE $E94B
// E937   20 59 EA   JSR $EA59     check EOI
// E93A   20 C0 E9   JSR $E9C0     read IEEE port
// E93D   29 01      AND #$01      isolate data bit
// E93F   D0 F6      BNE $E937
// E941   20 59 EA   JSR $EA59     check EOI
// E944   20 C0 E9   JSR $E9C0     read IEEE port
// E947   29 01      AND #$01      isolate data bit
// E949   F0 F6      BEQ $E941
// E94B   20 AE E9   JSR $E9AE     CLOCK OUT hi (RELEASED)
// E94E   20 59 EA   JSR $EA59     check EOI
// E951   20 C0 E9   JSR $E9C0     read IEEE port
// E954   29 01      AND #$01      isolate data bit
// E956   D0 F3      BNE $E94B
// E958   A9 08      LDA #$08      counter to 8 bits for serial
// E95A   85 98      STA $98       transmission
// E95C   20 C0 E9   JSR $E9C0     read IEEE port
// E95F   29 01      AND #$01      isolate data bit
// E961   D0 36      BNE $E999
// E963   A6 82      LDX $82
// E965   BD 3E 02   LDA $023E,X
// E968   6A         ROR           lowest data bit in carry
// E969   9D 3E 02   STA $023E,X
// E96C   B0 05      BCS $E973     set bit
// E96E   20 A5 E9   JSR $E9A5     DATA OUT, output bit '0'
// E971   D0 03      BNE $E976     absolute jump
// E973   20 9C E9   JSR $E99C     DATA OUT, output bit '1'
// E976   20 B7 E9   JSR $E9B7     set CLOCK OUT
// E979   A5 23      LDA $23
// E97B   D0 03      BNE $E980
// E97D   20 F3 FE   JSR $FEF3     delay for serial bus
// E980   20 FB FE   JSR $FEFB     set DATA OUT and CLOCK OUT
// E983   C6 98      DEC $98       all bits output?
// E985   D0 D5      BNE $E95C     no
// E987   20 59 EA   JSR $EA59     check EOI
// E98A   20 C0 E9   JSR $E9C0     read IEEE port
// E98D   29 01      AND #$01      isolate data bit
// E98F   F0 F6      BEQ $E987
// E991   58         CLI
// E992   20 AA D3   JSR $D3AA     get next data byte
// E995   78         SEI
// E996   4C 0F E9   JMP $E90F     and output

// E999   4C 4E EA   JMP $EA4E     to delay loop

bool IecProtocolSerial::sendByte(uint8_t data, bool eoi)
{
    IEC.pull ( PIN_IEC_SRQ );

    IEC.flags &= CLEAR_LOW; // $E85E

    // E91F   20 B7 E9   JSR $E9B7     CLOCK OUT lo (RELEASED)
    IEC.release ( PIN_IEC_CLK_OUT ); // $E94B/

    // Wait for listener to be ready
    // Either  the  talker  will pull the
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

        // Signal eoi by waiting 200 us
        // if ( !wait ( TIMING_Tye ) ) return false;

        // get eoi acknowledge:
        // E941   20 59 EA   JSR $EA59     check EOI
        // E944   20 C0 E9   JSR $E9C0     read IEEE port
        // E947   29 01      AND #$01      isolate data bit
        // E949   F0 F6      BEQ $E941
        if ( timeoutWait ( PIN_IEC_DATA_IN, PULLED ) == TIMED_OUT )
        {
            Debug_printv ( "EOI ACK: Listener didn't PULL DATA" );
            return false; // return error because timeout
        }
    }

    // Say we're ready
    // E94B   20 AE E9   JSR $E9AE     CLOCK OUT hi (PULLED)
    IEC.pull ( PIN_IEC_CLK_OUT );  // tell listner to wait

    // When  the  listener  is  ready  to  listen,  it  releases  the  Data
    // line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false
    // only when all listeners have RELEASED it - in other words, when  all  listeners  are  ready
    // to  accept  data.
    // E94E   20 59 EA   JSR $EA59     check EOI
    // E951   20 C0 E9   JSR $E9C0     read IEEE port
    // E954   29 01      AND #$01      isolate data bit
    // E956   D0 F3      BNE $E94B
    IEC.pull ( PIN_IEC_SRQ );
    if ( timeoutWait ( PIN_IEC_DATA_IN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        //Debug_printv ( "Wait for listener to be ready" );
        return false; // return error because of ATN or timeout
    }
    IEC.release ( PIN_IEC_SRQ );

    // STEP 3: SENDING THE BITS
    //IEC.pull ( PIN_IEC_SRQ );
    if ( !sendBits( data ) ) {
        Debug_printv ( "Error sending bits - byte '%02X'", data );
        return false;
    }
    //IEC.release ( PIN_IEC_SRQ );

    // STEP 4: FRAME HANDSHAKE
    // After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true
    // and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data
    // line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within
    // one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

    // Wait for listener to accept data
    // E987   20 59 EA   JSR $EA59     check EOI
    // E98A   20 C0 E9   JSR $E9C0     read IEEE port
    // E98D   29 01      AND #$01      isolate data bit
    // E98F   F0 F6      BEQ $E987
    //IEC.pull ( PIN_IEC_SRQ );
    if ( timeoutWait ( PIN_IEC_DATA_IN, PULLED, TIMEOUT_Tf ) >= TIMEOUT_Tf )
    {
        Debug_printv ( "Wait for listener to acknowledge byte received (pull data)" );
        return false; // return error because timeout
    }
    //IEC.release ( PIN_IEC_SRQ );

    // STEP 5: START OVER
    // We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,
    // and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has
    // happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause,
    // the Clock and Data lines are RELEASED to false and transmission stops.

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

// bool IecProtocolSerial::sendBits ( uint8_t data )
// {
//     // Send bits
//     for ( uint8_t n = 0; n < 8; n++ )
//     {
//         // tell listner to wait
//         // we control both CLOCK & DATA now
//         IEC.pull ( PIN_IEC_CLK_OUT );
//         if ( !wait ( TIMING_Ts1 ) ) return false; // 57us 

//         // set bit
//         ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
//         data >>= 1; // get next bit
//         if ( !wait ( TIMING_Ts2 ) ) return false; // 28us

//         // tell listener bit is ready to read
//         IEC.release ( PIN_IEC_CLK_OUT );
//         if ( !wait ( TIMING_Tv ) ) return false; // 76us 

//         // Release data line after bit sent
//         IEC.release ( PIN_IEC_DATA_OUT );
//     }
//     // Release data line after bit sent
//     // IEC.release ( PIN_IEC_DATA_OUT );

//     IEC.pull ( PIN_IEC_CLK_OUT );

//     return true;
// } // sendBits

bool IecProtocolSerial::sendBits ( uint8_t data )
{
    uint8_t tv = TIMING_Tv64; // C64 data valid timing

    // We can send faster if in VIC20 Mode
    if ( IEC.flags & VIC20_MODE )
    {
        tv = TIMING_Tv; // VIC-20 data valid timing
    }

    // Setup 8 bit counter
    // E958   A9 08      LDA #$08      counter to 8 bits for serial
    // E95A   85 98      STA $98       transmission
    uint8_t n = 8; // $E958

    // If data is not released exit
    // E95C   20 C0 E9   JSR $E9C0     read IEEE port
    // E95F   29 01      AND #$01      isolate data bit
    // E961   D0 36      BNE $E999

    // Send bits
    // ISR01 $E95C
    do
    {
        // set bit
        // ISR02 $E963-$E973
        // E963   A6 82      LDX $82
        // E965   BD 3E 02   LDA $023E,X
        // E968   6A         ROR           lowest data bit in carry
        // E969   9D 3E 02   STA $023E,X
        // E96C   B0 05      BCS $E973     set bit
        // E96E   20 A5 E9   JSR $E9A5     DATA OUT, output bit '0'
        // E971   D0 03      BNE $E976     absolute jump
        // E973   20 9C E9   JSR $E99C     DATA OUT, output bit '1'
        ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
        if ( !wait ( TIMING_Ts2 ) ) return false;
        data >>= 1; // get next bit

        // ISRCLK $E976-$E97D
        // E976   20 B7 E9   JSR $E9B7     set CLOCK OUT
        // E979   A5 23      LDA $23
        // E97B   D0 03      BNE $E980
        // E97D   20 F3 FE   JSR $FEF3     delay for serial bus
        IEC.release ( PIN_IEC_CLK_OUT ); // tell listener bit is ready to read
        if ( !wait ( tv ) ) return false;

        // ISR03 $E980
        IEC.pull ( PIN_IEC_DATA_OUT ); // pull clock line after bit sent
        IEC.release ( PIN_IEC_DATA_OUT ); // release data line after bit sent
    }
    // E983   C6 98      DEC $98       all bits output?
    // E985   D0 D5      BNE $E95C     no
    while ( n-- ); // $E985

    return true;
} // sendBit




// ******************************  DATA OUT lo (PULLED)
// E99C   AD 00 18   LDA $1800
// E99F   29 FD      AND #$FD      output bit '1'
// E9A1   8D 00 18   STA $1800
// E9A4   60         RTS

// ******************************  DATA OUT hi (RELEASED)
// E9A5   AD 00 18   LDA $1800
// E9A8   09 02      ORA #$02      output bit '0'
// E9AA   8D 00 18   STA $1800
// E9AD   60         RTS

// ******************************  CLOCK OUT hi (RELEASED)
// E9AE   AD 00 18   LDA $1800
// E9B1   09 08      ORA #$08      set bit 3
// E9B3   8D 00 18   STA $1800
// E9B6   60         RTS

// ******************************  CLOCK OUT lo (PULLED)
// E9B7   AD 00 18   LDA $1800
// E9BA   29 F7      AND #$F7      erase bit 3
// E9BC   8D 00 18   STA $1800
// E9BF   60         RTS

// ******************************  read IEEE port
// E9C0   AD 00 18   LDA $1800     read port
// E9C3   CD 00 18   CMP $1800     wait for constants
// E9C6   D0 F8      BNE $E9C0
// E9C8   60         RTS

// E9C9   A9 08      LDA #$08
// E9CB   85 98      STA $98       bit counter for serial output
// E9CD   20 59 EA   JSR $EA59     check EOI
// E9D0   20 C0 E9   JSR $E9C0     read IEEE port
// E9D3   29 04      AND #$04      CLOCK IN?
// E9D5   D0 F6      BNE $E9CD     no, wait
// E9D7   20 9C E9   JSR $E99C     DATA OUT, bit '1'
// E9DA   A9 01      LDA #$01
// E9DC   8D 05 18   STA $1805     set timer
// E9DF   20 59 EA   JSR $EA59     check EOI
// E9E2   AD 0D 18   LDA $180D
// E9E5   29 40      AND #$40      timer run down?
// E9E7   D0 09      BNE $E9F2     yes, EOI
// E9E9   20 C0 E9   JSR $E9C0     read IEEE port
// E9EC   29 04      AND #$04      CLOCK IN?
// E9EE   F0 EF      BEQ $E9DF     no, wait
// E9F0   D0 19      BNE $EA0B
// E9F2   20 A5 E9   JSR $E9A5     DATA OUT bit '0' hi
// E9F5   A2 0A      LDX #$0A      10
// E9F7   CA         DEX           delay loop, approx 50 micro sec.
// E9F8   D0 FD      BNE $E9F7
// E9FA   20 9C E9   JSR $E99C     DATA OUT, bit '1', lo
// E9FD   20 59 EA   JSR $EA59     check EOI
// EA00   20 C0 E9   JSR $E9C0     read IEEE
// EA03   29 04      AND #$04      CLOCK IN?
// EA05   F0 F6      BEQ $E9FD     no, wait
// EA07   A9 00      LDA #$00
// EA09   85 F8      STA $F8       set EOI flag
// EA0B   AD 00 18   LDA $1800     IEEE port
// EA0E   49 01      EOR #$01      invert data byte
// EA10   4A         LSR A
// EA11   29 02      AND #$02
// EA13   D0 F6      BNE $EA0B     CLOCK IN?
// EA15   EA         NOP
// EA16   EA         NOP
// EA17   EA         NOP
// EA18   66 85      ROR $85       prepare next bit
// EA1A   20 59 EA   JSR $EA59     check EOI
// EA1D   20 C0 E9   JSR $E9C0     read IEEE port
// EA20   29 04      AND #$04      CLOCK IN?
// EA22   F0 F6      BEQ $EA1A     no
// EA24   C6 98      DEC $98       decrement bit counter
// EA26   D0 E3      BNE $EA0B     all bits output?
// EA28   20 A5 E9   JSR $E9A5     DATA OUT, bit '0', hi
// EA2B   A5 85      LDA $85       load data byte again
// EA2D   60         RTS

// ******************************  accept data from serial bus
// EA2E   78         SEI
// EA2F   20 07 D1   JSR $D107     open channel for writing
// EA32   B0 05      BCS $EA39     channel not active?
// EA34   B5 F2      LDA $F2,X     WRITE flag
// EA36   6A         ROR
// EA37   B0 0B      BCS $EA44     not set?
// EA39   A5 84      LDA $84       secondary address
// EA3B   29 F0      AND #$F0
// EA3D   C9 F0      CMP #$F0      OPEN command?
// EA3F   F0 03      BEQ $EA44     yes
// EA41   4C 4E EA   JMP $EA4E     to wait loop

// EA44   20 C9 E9   JSR $E9C9     get data byte from bus
// EA47   58         CLI
// EA48   20 B7 CF   JSR $CFB7     and write in buffer
// EA4B   4C 2E EA   JMP $EA2E     to loop beginning

// EA4E   A9 00      LDA #$00
// EA50   8D 00 18   STA $1800     reset IEEE port
// EA53   4C E7 EB   JMP $EBE7     to wait loop

// EA56   4C 5B E8   JMP $E85B     to serial bus main loop

// ******************************
// EA59   A5 7D      LDA $7D       EOI received?
// EA5B   F0 06      BEQ $EA63     yes
// EA5D   AD 00 18   LDA $1800     IEEE port
// EA60   10 09      BPL $EA6B
// EA62   60         RTS

// EA63   AD 00 18   LDA $1800     IEEE port
// EA66   10 FA      BPL $EA62
// EA68   4C 5B E8   JMP $E85B     to serial bus main loop
// EA6B   4C D7 E8   JMP $E8D7     set EOI, serial bus

int16_t IecProtocolSerial::receiveByte()
{
// STEP 1: READY TO RECEIVE
// Sooner or later, the talker will want to talk, and send a character.
// When it's ready to go, it releases the Clock line to false.  This signal change might be
// translated as "I'm ready to send a character." The listener must detect this and respond,
// but it doesn't have to do so immediately. The listener will respond  to  the  talker's
// "ready  to  send"  signal  whenever  it  likes;  it  can  wait  a  long  time.    If  it's
// a printer chugging out a line of print, or a disk drive with a formatting job in progress,
// it might holdback for quite a while; there's no time limit.

    IEC.flags &= CLEAR_LOW;

    // Sometimes the C64 pulls ATN but doesn't pull CLOCK right away
    if ( !wait ( TIMING_STABLE ) ) return -1;

    // Wait for talker ready
    if ( timeoutWait ( PIN_IEC_CLK_IN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        Debug_printv ( "Wait for talker ready" );
        return -1; // return error because timeout
    }

    // Say we're ready
    // STEP 2: READY FOR DATA
    // When  the  listener  is  ready  to  listen,  it  releases  the  Data
    // line  to  false.    Suppose  there  is  more  than one listener.  The Data line will go false
    // only when all listeners have RELEASED it - in other words, when  all  listeners  are  ready
    // to  accept  data.  What  happens  next  is  variable.
    if ( !wait ( TIMING_Th ) ) return -1;
    IEC.release ( PIN_IEC_DATA_OUT );

    // Wait for all other devices to release the data line
    if ( timeoutWait ( PIN_IEC_DATA_IN, RELEASED, FOREVER ) == TIMED_OUT )
    {
        Debug_printv ( "Wait for all other devices to release the data line" );
        return -1; // return error because timeout
    }


    // Either  the  talker  will pull the
    // Clock line back to true in less than 200 microseconds - usually within 60 microseconds - or it
    // will  do  nothing.    The  listener  should  be  watching,  and  if  200  microseconds  pass
    // without  the Clock line going to true, it has a special task to perform: note EOI.

    // pull ( PIN_IEC_SRQ );
    if ( timeoutWait ( PIN_IEC_CLK_IN, PULLED, TIMEOUT_Tne, false ) >= TIMEOUT_Tne )
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

        // Debug_printv("EOI!");

        IEC.flags |= EOI_RECVD;

        // Acknowledge by pull down data more than 60us
        IEC.pull ( PIN_IEC_DATA_OUT );
        if ( !wait ( TIMING_Tei ) ) return -1;
        IEC.release ( PIN_IEC_DATA_OUT );

        // but still wait for CLK to be PULLED
        // Is this an empty stream?
        if ( timeoutWait ( PIN_IEC_CLK_IN, PULLED, TIMING_EMPTY ) >= TIMING_EMPTY )
        {
            Debug_printv ( "empty stream signaled" );
            IEC.flags |= EMPTY_STREAM;
            return -1; // return error because empty stream
        }
    }
    else
    {
        if ( !wait ( TIMING_Tne ) ) return -1;
    }
    // release ( PIN_IEC_SRQ );


    // STEP 3: RECEIVING THE BITS
    //pull ( PIN_IEC_SRQ );
    uint8_t data = receiveBits();
    //release ( PIN_IEC_SRQ );
    if ( IEC.flags & ERROR)
        return -1;

    // STEP 4: FRAME HANDSHAKE
    // After the eighth bit has been sent, it's the listener's turn to acknowledge.  At this moment, the Clock line  is  true
    // and  the  Data  line  is  false.    The  listener  must  acknowledge  receiving  the  byte  OK  by pulling the Data
    // line to true. The talker is now watching the Data line.  If the listener doesn't pull the  Data  line  true  within
    // one  millisecond  -  one  thousand  microseconds  -  it  will  know  that something's wrong and may alarm appropriately.

    // Acknowledge byte received
    if ( !wait ( TIMING_Tf ) ) return -1;
    IEC.pull ( PIN_IEC_DATA_OUT );

    // STEP 5: START OVER
    // We're  finished,  and  back  where  we  started.    The  talker  is  holding  the  Clock  line  true,
    // and  the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has
    // happened. If EOI was sent or received in this last transmission, both talker and listener "letgo."  After a suitable pause,
    // the Clock and Data lines are RELEASED to false and transmission stops.

    if ( IEC.flags & EOI_RECVD )
    {
        // EOI Received
        if ( !wait ( TIMING_Tfr ) ) return -1;
        //IEC.release ( PIN_IEC_DATA_OUT );
    }
    else
    {
         wait ( TIMING_Tbb );
    }

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
int16_t IecProtocolSerial::receiveBits ()
{
    // Listening for bits
    uint8_t data = 0;
    int16_t bit_time;  // Used to detect JiffyDOS

    uint8_t n = 0;

    for ( n = 0; n < 8; n++ )
    {
        data >>= 1;

        do
        {
            // wait for bit to be ready to read
            //IEC.pull ( PIN_IEC_SRQ );
            bit_time = timeoutWait ( PIN_IEC_CLK_IN, RELEASED, TIMEOUT_DEFAULT, false );

            // // If the bit time is less than 40us we are talking with a VIC20
            // if ( bit_time < TIMING_VIC20_DETECT )
            //     IEC.flags |= VIC20_MODE;

            // If there is a delay before the last bit, the controller uses JiffyDOS
            if ( n == 7 && bit_time >= TIMING_JIFFY_DETECT )
            {
                if ( IEC.status ( PIN_IEC_ATN ) == PULLED && data < 0x60 )
                {
                    IEC.flags |= ATN_PULLED;

                    uint8_t device = data & 0x1F;
                    if ( IEC.enabledDevices & ( 1 << device ) )
                    {
                        /* If it's for us, notify controller that we support Jiffy too */
                        IEC.pull(PIN_IEC_DATA_OUT);
                        wait( TIMING_JIFFY_ACK, 0, false );
                        IEC.release(PIN_IEC_DATA_OUT);
                        IEC.flags |= JIFFY_ACTIVE;
                    }
                }
            }
            else if ( bit_time == TIMED_OUT )
            {
                Debug_printv ( "wait for bit to be ready to read, bit_time[%d] n[%d]", bit_time, n );
                return -1; // return error because timeout
            }
        } while ( bit_time >= TIMING_JIFFY_DETECT );
        
        // get bit
        data |= ( IEC.status ( PIN_IEC_DATA_IN ) == RELEASED ? ( 1 << 7 ) : 0 );
        //IEC.release ( PIN_IEC_SRQ );

        // wait for talker to finish sending bit
        if ( timeoutWait ( PIN_IEC_CLK_IN, PULLED ) == TIMED_OUT )
        {
            Debug_printv ( "wait for talker to finish sending bit n[%d]", n );
            return -1; // return error because timeout
        }
    }

    return data;
}



#endif

