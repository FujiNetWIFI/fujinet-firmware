#ifdef BUILD_IEC
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

#include "jiffydos.h"

#include <rom/ets_sys.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <freertos/semphr.h>
// #include <driver/timer.h>

#include "bus.h"
#include "_protocol.h"

#include "../../../include/debug.h"
#include "../../../include/pinmap.h"

using namespace Protocol;

/**
 * Callback function to set timeout 
 */
static void onTimer(void *arg)
{
    IECProtocol *p = (IECProtocol *)arg;
    p->timer_timedout = true;
}

JiffyDOS::JiffyDOS() {
    // 2bit Fast Loader Pair Timing
    bit_pair_timing.clear();
    bit_pair_timing = {
        {13, 9, 5, 5},       // Receive -2us for overhead
        {10, 10, 11, 10}     // Send
    };

    esp_timer_create_args_t args = {
        .callback = onTimer,
        .arg = this,
	.dispatch_method = ESP_TIMER_TASK,
        .name = nullptr,
	.skip_unhandled_events = 0,
    };
    esp_timer_create(&args, &timer_handle);
};

JiffyDOS::~JiffyDOS() {
    esp_timer_stop(timer_handle);
    esp_timer_delete(timer_handle);
};


uint8_t  JiffyDOS::receiveByte ()
{
    uint8_t data = 0;

    IEC.flags &= CLEAR_LOW;

    // Release the Data line to signal we are ready
#ifndef IEC_SPLIT_LINE
    //IEC_RELEASE(PIN_IEC_CLK_IN);
    IEC_RELEASE(PIN_IEC_DATA_IN);
#endif

    // Wait for talker ready
    while ( IEC_IS_ASSERTED( PIN_IEC_CLK_IN ) );

    // RECEIVING THE BITS
    // As soon as the talker releases the Clock line we are expected to receive the bits
    // Bits are inverted so use IEC_IS_ASSERTED() to get asserted/released status

    //IEC_ASSERT( PIN_IEC_SRQ );

    // get bits 4,5
    usleep ( bit_pair_timing[0][0] ); // Includes setup delay
    if ( IEC_IS_ASSERTED( PIN_IEC_CLK_IN ) )  data |= 0b00010000; // 0
    if ( IEC_IS_ASSERTED( PIN_IEC_DATA_IN ) ) data |= 0b00100000; // 1
    IEC_ASSERT( PIN_IEC_SRQ );

    // get bits 6,7
    usleep ( bit_pair_timing[0][1] );
    if ( IEC_IS_ASSERTED( PIN_IEC_CLK_IN ) ) data |=  0b01000000; // 0
    if ( IEC_IS_ASSERTED( PIN_IEC_DATA_IN ) ) data |= 0b10000000; // 0
    IEC_RELEASE( PIN_IEC_SRQ );

    // get bits 3,1
    usleep ( bit_pair_timing[0][2] );
    if ( IEC_IS_ASSERTED( PIN_IEC_CLK_IN ) )  data |= 0b00001000; // 0
    if ( IEC_IS_ASSERTED( PIN_IEC_DATA_IN ) ) data |= 0b00000010; // 0
    IEC_ASSERT( PIN_IEC_SRQ );

    // get bits 2,0
    usleep ( bit_pair_timing[0][3] );
    if ( IEC_IS_ASSERTED( PIN_IEC_CLK_IN ) )  data |= 0b00000100; // 1
    if ( IEC_IS_ASSERTED( PIN_IEC_DATA_IN ) ) data |= 0b00000001; // 0
    IEC_RELEASE( PIN_IEC_SRQ );

    // Check CLK for EOI
    usleep ( 12 );
    if ( IEC_IS_ASSERTED( PIN_IEC_CLK_IN ) )
        IEC.flags |= EOI_RECVD;
    IEC_ASSERT( PIN_IEC_SRQ );

    // Acknowledge byte received
    // If we want to indicate an error we can release DATA
    usleep ( 60 );
    IEC_ASSERT( PIN_IEC_DATA_OUT );
    usleep ( 10 );

    //IEC_RELEASE( PIN_IEC_SRQ );

    //Debug_printv("data[%02X] eoi[%d]", data, eoi); // $ = 0x24

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
bool JiffyDOS::sendByte ( uint8_t data, bool signalEOI )
{
    IEC.flags and_eq CLEAR_LOW;

    // Release the Data line to signal we are ready
#ifndef IEC_SPLIT_LINE
    IEC_RELEASE(PIN_IEC_CLK_IN);
    IEC_RELEASE(PIN_IEC_DATA_IN);
#endif

    // Wait for listener ready
    while ( IEC_IS_ASSERTED( PIN_IEC_DATA_IN ) );

    // STEP 2: SENDING THE BITS
    // As soon as the listener releases the DATA line we are expected to send the bits
    // Bits are inverted so use IEC_IS_ASSERTED() to get asserted/released status

    IEC_ASSERT( PIN_IEC_SRQ );

    // Start timer
    usleep ( bit_pair_timing[1][0] );

    // set bits 0,1
    //IEC_ASSERT( PIN_IEC_SRQ );
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_CLK_OUT ) : IEC_ASSERT( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_DATA_OUT ) : IEC_ASSERT( PIN_IEC_DATA_OUT );
    usleep ( bit_pair_timing[1][1] );

    // set bits 2,3
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_CLK_OUT ) : IEC_ASSERT( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_DATA_OUT ) : IEC_ASSERT( PIN_IEC_DATA_OUT );
    usleep ( bit_pair_timing[1][2] );

    // set bits 4,5
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_CLK_OUT ) : IEC_ASSERT( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_DATA_OUT ) : IEC_ASSERT( PIN_IEC_DATA_OUT );
    usleep ( bit_pair_timing[1][3] );

    // set bits 6,7
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_CLK_OUT ) : IEC_ASSERT( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC_RELEASE( PIN_IEC_DATA_OUT ) : IEC_ASSERT( PIN_IEC_DATA_OUT );
    usleep ( bit_pair_timing[1][4] );

    // Check CLK for EOI
    ( signalEOI ) ? IEC_ASSERT( PIN_IEC_CLK_OUT ) : IEC_RELEASE( PIN_IEC_CLK_OUT );
    usleep ( 13 );
    //IEC_RELEASE( PIN_IEC_SRQ );

    // Acknowledge byte received
    // If we want to indicate an error we can release DATA
//    bool error = IEC_IS_ASSERTED( PIN_IEC_DATA_IN );

    return true;
} // sendByte

#endif /* BUILD_IEC*/
