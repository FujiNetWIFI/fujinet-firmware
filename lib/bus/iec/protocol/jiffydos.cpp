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


int16_t  JiffyDOS::receiveByte ()
{
    uint8_t data = 0;

    IEC.flags and_eq CLEAR_LOW;

    // Release the Data line to signal we are ready
#ifndef IEC_SPLIT_LINE
    IEC.release(PIN_IEC_CLK_IN);
    IEC.release(PIN_IEC_DATA_IN);
#endif

    // Wait for talker ready
    while ( IEC.status( PIN_IEC_CLK_IN ) == PULLED );

    // RECEIVING THE BITS
    // As soon as the talker releases the Clock line we are expected to receive the bits
    // Bits are inverted so use IEC.status() to get pulled/released status

    IEC.pull ( PIN_IEC_SRQ );

    // Setup Delay
    //uint64_t cur_time = esp_timer_get_time();
    //uint64_t exp_time = 11;
    esp_rom_delay_us ( 11 );

    // get bits 4,5
    //IEC.pull ( PIN_IEC_SRQ );
    if ( IEC.status ( PIN_IEC_CLK_IN ) )  data |= 0b00010000; // 1
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b00100000; // 0
    //IEC.release ( PIN_IEC_SRQ );
    esp_rom_delay_us ( bit_pair_timing[0][0] );

    // get bits 6,7
    //IEC.pull ( PIN_IEC_SRQ );
    if ( IEC.status ( PIN_IEC_CLK_IN ) ) data |=  0b01000000; // 0
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b10000000; // 0
    //IEC.release ( PIN_IEC_SRQ );
    esp_rom_delay_us ( bit_pair_timing[1][1] );

    // get bits 3,1
    //IEC.pull ( PIN_IEC_SRQ );
    if ( IEC.status ( PIN_IEC_CLK_IN ) )  data |= 0b00001000; // 0
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b00000010; // 0
    //IEC.release ( PIN_IEC_SRQ );
    esp_rom_delay_us ( bit_pair_timing[0][2] );

    // get bits 2,0
    //IEC.pull ( PIN_IEC_SRQ );
    if ( IEC.status ( PIN_IEC_CLK_IN ) )  data |= 0b00000100; // 1
    if ( IEC.status ( PIN_IEC_DATA_IN ) ) data |= 0b00000001; // 0
    //IEC.release ( PIN_IEC_SRQ );
    esp_rom_delay_us ( bit_pair_timing[0][3] );

    // Acknowledge byte received
    // If we want to indicate an error we can release DATA
    IEC.pull ( PIN_IEC_DATA_OUT );

    // Check CLK for EOI
    //bool eoi = gpio_get_level ( PIN_IEC_CLK_IN );
    //esp_rom_delay_us ( 15 );

    IEC.release ( PIN_IEC_SRQ );
    esp_rom_delay_us ( 158 );

    //if ( eoi ) IEC.flags |= EOI_RECVD;
    //Debug_printv("data[%02X] eoi[%d]", data, eoi); // $ = 0x24

    return (uint8_t) (data & 0xFF);
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
    IEC.pull(PIN_IEC_CLK_IN);
    IEC.pull(PIN_IEC_DATA_IN);
#endif

    // Wait for listener ready
    esp_rom_delay_us ( 10 );
    IEC.release ( PIN_IEC_CLK_OUT );
    esp_rom_delay_us ( 10 );
    IEC.release ( PIN_IEC_CLK_OUT );

    // STEP 2: SENDING THE BITS
    // As soon as the listener releases the DATA line we are expected to send the bits
    // Bits are inverted so use IEC.status() to get pulled/released status

    //IEC.pull ( PIN_IEC_SRQ );

    // Start timer
    esp_rom_delay_us ( 37 );

    // set bits 0,1
    //IEC.pull ( PIN_IEC_SRQ );
    ( data & 1 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    esp_rom_delay_us ( bit_pair_timing[1][0] );

    // set bits 2,3
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    esp_rom_delay_us ( bit_pair_timing[1][1] );

    // set bits 4,5
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    esp_rom_delay_us ( bit_pair_timing[1][2] );

    // set bits 6,7
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_CLK_OUT ) : IEC.pull ( PIN_IEC_CLK_OUT );
    data >>= 1; // shift to next bit
    ( data & 1 ) ? IEC.release ( PIN_IEC_DATA_OUT ) : IEC.pull ( PIN_IEC_DATA_OUT );
    esp_rom_delay_us ( bit_pair_timing[1][3] );

    // Acknowledge byte received
    // If we want to indicate an error we can release DATA
//    bool error = IEC.status ( PIN_IEC_DATA_IN );

    // Check CLK for EOI
    ( signalEOI ) ? IEC.pull ( PIN_IEC_CLK_OUT ) : IEC.release ( PIN_IEC_CLK_OUT );
    esp_rom_delay_us ( 13 );
    //IEC.release ( PIN_IEC_SRQ );

    return true;
} // sendByte

#endif /* BUILD_IEC*/