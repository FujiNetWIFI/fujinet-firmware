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

#define DYNAMIC_DELAY

#include "cpbstandardserial.h"

#include "bus.h"
#include "_protocol.h"

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

#define IEC_SET_STATE(x) ({IEC._state = x;})

using namespace Protocol;

// STEP 1: READY TO RECEIVE
// Sooner or later, the talker will want to talk, and send a
// character. When it's ready to go, it releases the Clock line.
// This signal change might be translated as "I'm ready to send a
// character." The listener must detect this and respond, but it
// doesn't have to do so immediately. The listener will respond to the
// talker's "ready to send" signal whenever it likes; it can wait a
// long time. If it's a printer chugging out a line of print, or a
// disk drive with a formatting job in progress, it might holdback for
// quite a while; there's no time limit.
uint8_t CPBStandardSerial::receiveByte()
{
  int abort;
  int idx, data;
  uint64_t start, now;
  int elapsed = 0;


  IEC.flags &= CLEAR_LOW;
  if (IEC_IS_ASSERTED(PIN_IEC_ATN))
    IEC.flags |= ATN_ASSERTED;

  portDISABLE_INTERRUPTS();

#ifdef DYNAMIC_DELAY
  transferDelaySinceLast(TIMING_Tf);
#endif

  // STEP 2: READY FOR DATA
  // line. Suppose there is more than one listener. The Data line
  // will be reelased only when all listeners have RELEASED it - in
  // other words, when all listeners are ready to accept data. What
  // happens next is variable.

  // Release Data and wait for all other devices to release the data line too

  IEC_RELEASE(PIN_IEC_DATA_OUT);

  // Either the talker will assert the Clock line back to asserted
  // in less than 200 microseconds - usually within 60 microseconds
  // - or it will do nothing. The listener should be watching, and
  // if 200 microseconds pass without the Clock line being asserted,
  // it has a special task to perform: note EOI.
  start = esp_timer_get_time();
  for (abort = 0; !abort && !IEC_IS_ASSERTED(PIN_IEC_CLK_IN); ) {
    now = esp_timer_get_time();
    elapsed = now - start;

    if (!(IEC.flags & EOI_RECVD) && elapsed >= TIMING_Tye) {
      // INTERMISSION: EOI
      // If the Ready for Data signal isn't acknowledged by the
      // talker within 200 microseconds, the listener knows that the
      // talker is trying to signal EOI. EOI, which formally stands
      // for "End of Indicator," means "this character will be the
      // last one."  If it's a sequential disk file, don't ask for
      // more: there will be no more. If it's a relative record,
      // that's the end of the record. The character itself will
      // still be coming, but the listener should note: here comes
      // the last character. So if the listener sees the 200
      // microsecond time-out, it must signal "OK, I noticed the
      // EOI" back to the talker, It does this by asserting the Data
      // line for at least 60 microseconds, and then releasing it.
      // The talker will then revert to transmitting the character
      // in the usual way; within 60 microseconds it will assert the
      // Clock line, and transmission will continue. At this point,
      // the Clock line is asserted whether or not we have gone
      // through the EOI sequence; we're back to a common
      // transmission sequence.
      IEC_ASSERT(PIN_IEC_DATA_OUT);
      usleep(TIMING_Tei);
      IEC_RELEASE(PIN_IEC_DATA_OUT);
      IEC.flags |= EOI_RECVD;
    }

    if (elapsed > 100000) {
      abort = 1;
      break;
    }
  }

  // STEP 3: RECEIVING THE BITS
  // The talker has eight bits to send. They will go out without
  // handshake; in other words, the listener had better be there to
  // catch them, since the talker won't wait to hear from the listener.
  // At this point, the talker controls both lines, Clock and Data. At
  // the beginning of the sequence, it is holding the Clock asserted,
  // while the Data line is RELEASED. the Data line will change soon,
  // since we'll sendthe data over it. The eights bits will go out from
  // the character one at a time, with the least significant bit going
  // first. For example, if the character is the ASCII question mark,
  // which is written in binary as 00011111, the ones will go out first,
  // followed by the zeros. Now, for each bit, we set the Data line
  // released (one) or asserted (zero) according to whether the bit is
  // one or zero. As soon as that's set, the Clock line is RELEASED,
  // signalling "data ready."  The talker will typically have a bit in
  // place and be signalling ready in 70 microseconds or less. Once the
  // talker has signalled "data ready," it will hold the two lines
  // steady for at least 20 microseconds timing needs to be increased to
  // 60 microseconds if the Commodore 64 is listening, since the 64's
  // video chip may interrupt the processor for 42 microseconds at a
  // time, and without the extra wait the 64 might completely miss a
  // bit. The listener plays a passive role here; it sends nothing, and
  // just watches. As soon as it sees the Clock line released, it grabs
  // the bit from the Data line and puts it away. It then waits for the
  // clock line to be asserted, in order to prepare for the next
  // bit. When the talker figures the data has been held for a
  // sufficient length of time, it asserts the Clock line and releases
  // the Data line. Then it starts to prepare the next bit.

#ifndef JIFFYDOS
  for (idx = data = 0; !abort && idx < 8; idx++) {
#else
  for (idx = data = 0; !abort && idx < 7; idx++) {
#endif
    if ((abort = waitForSignals(PIN_IEC_CLK_IN, IEC_RELEASED, 0, 0, TIMEOUT_DEFAULT)))
      break;

    if (!IEC_IS_ASSERTED(PIN_IEC_DATA_IN))
      data |= 1 << idx;

    if (waitForSignals(PIN_IEC_CLK_IN, IEC_ASSERTED, 0, 0, TIMEOUT_DEFAULT)) {
      if (idx < 7)
        abort = 1;
    }
  }

#ifdef JIFFYDOS
  // If there is a 218us delay before bit 7, the controller uses JiffyDOS
  if (waitForSignals(PIN_IEC_CLK_IN, IEC_RELEASED, 0, 0,
		     TIMING_PROTOCOL_DETECT) == TIMED_OUT) {
    // acknowledge we support JiffyDOS
    IEC_ASSERT(PIN_IEC_DATA_OUT);
    usleep(TIMING_PROTOCOL_ACK);
    IEC_RELEASE(PIN_IEC_DATA_OUT);
    IEC.flags |= JIFFYDOS_ACTIVE;

    abort = waitForSignals(PIN_IEC_CLK_IN, IEC_RELEASED, 0, 0, TIMEOUT_DEFAULT);
  }
#endif

  if (!abort) {
    // JiffyDOS check complete, Get last bit
    if (!IEC_IS_ASSERTED(PIN_IEC_DATA_IN))
      data |= 1 << idx;

    waitForSignals(PIN_IEC_CLK_IN, IEC_ASSERTED, 0, 0, TIMEOUT_DEFAULT);
  }

  portENABLE_INTERRUPTS();

  // STEP 4: FRAME HANDSHAKE
  // After the eighth bit has been sent, it's the listener's turn to
  // acknowledge. At this moment, the Clock line is asserted and the
  // Data line is released. The listener must acknowledge receiving
  // the byte OK by asserting the Data line. The talker is now
  // watching the Data line. If the listener doesn't assert the Data
  // line within one millisecond - one thousand microseconds - it
  // will know that something's wrong and may alarm appropriately.
  IEC_ASSERT(PIN_IEC_DATA_OUT);

#ifdef DYNAMIC_DELAY
  transferEnd();
#else
  //usleep(TIMING_Tf);
#endif

  // STEP 5: START OVER
  // We're finished, and back where we started. The talker is
  // asserting the Clock line, and the listener is asserting the
  // Data line. We're ready for step 1; we may send another
  // character - unless EOI has happened. If EOI was sent or
  // received in this last transmission, both talker and listener
  // "letgo."  After a suitable pause, the Clock and Data lines are
  // RELEASED and transmission stops.

  if (abort)
    return -1;

  return data;
}

// STEP 1: READY TO SEND
// Sooner or later, the talker will want to talk, and send a
// character. When it's ready to go, it releases the Clock line. This
// signal change might be translated as "I'm ready to send a
// character." The listener must detect this and respond, but it
// doesn't have to do so immediately. The listener will respond to the
// talker's "ready to send" signal whenever it likes; it can wait a
// long time. If it's a printer chugging out a line of print, or a
// disk drive with a formatting job in progress, it might holdback for
// quite a while; there's no time limit.
bool CPBStandardSerial::sendByte(uint8_t data, bool eoi)
{
  int len;
  int abort = 0;
  uint8_t Tv = TIMING_Tv64;


  if (0) //(IEC.vic20_mode)
    Tv = TIMING_Tv;

  if (IEC_IS_ASSERTED(PIN_IEC_ATN) || IEC._state > BUS_IDLE) {
    Debug_printv("Abort");
    return 0;
  }

  //IEC_ASSERT(PIN_IEC_SRQ);//Debug
  gpio_intr_disable(PIN_IEC_CLK_IN);
  portDISABLE_INTERRUPTS();

#ifdef DYNAMIC_DELAY
  transferDelaySinceLast(TIMING_Tbb);
#endif

  IEC_RELEASE(PIN_IEC_CLK_OUT);

  // STEP 2: READY FOR DATA
  // When the listener is ready to listen, it releases the Data
  // line. Suppose there is more than one listener. The Data line
  // will be released only when ALL listeners have RELEASED it - in
  // other words, when all listeners are ready to accept data.
  // IEC_ASSERT( PIN_IEC_SRQ );

  // FIXME - Can't wait FOREVER because watchdog will get
  //         mad. Probably need to configure DATA GPIO with POSEDGE
  //         interrupt and not do portDISABLE_INTERRUPTS(). Without
  //         interrupts disabled though there is a big risk of false
  //         EOI being sent. Maybe the DATA ISR needs to handle EOI
  //         signaling too?

  if ((abort = waitForSignals(PIN_IEC_DATA_IN, IEC_RELEASED, PIN_IEC_ATN, IEC_ASSERTED, FOREVER))) {
    Debug_printv("data released abort");
  }

  /* Because interrupts are disabled it's possible to miss the ATN pause signal */
  if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
    abort = 1;
    Debug_printv("ATN abort");
  }

  // What happens next is variable. Either the talker will assert
  // the Clock line in less than 200 microseconds - usually within
  // 60 microseconds - or it will do nothing. The listener should be
  // watching, and if 200 microseconds pass without the Clock line
  // being asserted, it has a special task to perform: note EOI.
  if (!abort && eoi) {
    // INTERMISSION: EOI
    // If the Ready for Data signal isn't acknowledged by the
    // talker within 200 microseconds, the listener knows that the
    // talker is trying to signal EOI. EOI, which formally stands
    // for "End of Indicator," means "this character will be the
    // last one." If it's a sequential disk file, don't ask for
    // more: there will be no more. If it's a relative record,
    // that's the end of the record. The character itself will
    // still be coming, but the listener should note: here comes
    // the last character. So if the listener sees the 200
    // microsecond time-out, it must signal "OK, I noticed the
    // EOI" back to the talker, It does this by asserting the Data
    // line for at least 60 microseconds, and then releasing
    // it. The talker will then revert to transmitting the
    // character in the usual way; within 60 microseconds it will
    // assert the Clock line, and transmission will continue.  At
    // this point, the Clock line is asserted whether or not we
    // have gone through the EOI sequence; we're back to a common
    // transmission sequence.

    // Wait for EOI ACK
    // This will happen after appx 250us
    if ((abort = waitForSignals(PIN_IEC_DATA_IN, IEC_ASSERTED, PIN_IEC_ATN, IEC_ASSERTED, TIMEOUT_Tf))) {
      Debug_printv("EOI ack abort");
    }

    if (!abort &&
        (abort = waitForSignals(PIN_IEC_DATA_IN, IEC_RELEASED, PIN_IEC_ATN, IEC_ASSERTED, TIMEOUT_Tne))) {
      Debug_printv("EOI ackack abort");
    }
  }

  IEC_ASSERT(PIN_IEC_CLK_OUT);
  usleep(TIMING_Tpr);

  // STEP 3: SENDING THE BITS
  for (len = 0; !abort && len < 8; len++, data >>= 1) {
    if (IEC_IS_ASSERTED(PIN_IEC_ATN)) {
      Debug_printv("ATN 2 abort");
      abort = 1;
      break;
    }

    if (data & 1)
      IEC_RELEASE(PIN_IEC_DATA_OUT);
    else
      IEC_ASSERT(PIN_IEC_DATA_OUT);

    usleep(TIMING_Ts);
    IEC_RELEASE(PIN_IEC_CLK_OUT);
    usleep(Tv);
    IEC_RELEASE(PIN_IEC_DATA_OUT);
    IEC_ASSERT(PIN_IEC_CLK_OUT);
  }

  // STEP 4: FRAME HANDSHAKE
  // After the eighth bit has been sent, it's the listener's turn to
  // acknowledge. At this moment, the Clock line is asserted and the
  // Data line is released. The listener must acknowledge receiving
  // the byte OK by asserting the Data line. The talker is now
  // watching the Data line. If the listener doesn't assert the Data
  // line within one millisecond - one thousand microseconds - it
  // will know that something's wrong and may alarm appropriately.

  // Wait for listener to accept data
  if (!abort &&
      (abort = waitForSignals(PIN_IEC_DATA_IN, IEC_ASSERTED, PIN_IEC_ATN, IEC_ASSERTED, TIMEOUT_Tf))) {
    // RECIEVER TIMEOUT
    // If no receiver asserts DATA within 1000 µs at the end of
    // the transmission of a byte (after step 28), a receiver
    // timeout is raised.
    if (!IEC_IS_ASSERTED(PIN_IEC_ATN)) {
      abort = 0;
    }
    else {
      IEC_SET_STATE(BUS_IDLE);
    }
  }
  portENABLE_INTERRUPTS();
  gpio_intr_enable(PIN_IEC_CLK_IN);
  //IEC_RELEASE(PIN_DEBUG);//Debug

#ifdef DYNAMIC_DELAY
  transferEnd();
#else
  //usleep(TIMING_Tbb);
#endif

  // STEP 5: START OVER
  // We're finished, and back where we started. The talker is
  // asserting the Clock line, and the listener is asserting the
  // Data line. We're ready for step 1; we may send another
  // character - unless EOI has happened. If EOI was sent or
  // received in this last transmission, both talker and listener
  // "letgo."  After a suitable pause, the Clock and Data lines are
  // RELEASED and transmission stops.

  if (abort && IEC_IS_ASSERTED(PIN_IEC_ATN))
    IEC_RELEASE(PIN_IEC_CLK_OUT);

  return !abort;
}

#endif // BUILD_IEC
