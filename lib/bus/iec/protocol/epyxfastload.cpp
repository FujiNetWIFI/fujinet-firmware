// /* sd2iec - SD/MMC to Commodore serial bus interface/controller
//    Copyright (C) 2007-2017  Ingo Korb <ingo@akana.de>

//    Inspired by MMC2IEC by Lars Pontoppidan et al.

//    FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License only.

//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


//    llfl-epyxcart.c: Low level handling of Epyx Fastload Cart loader

// */

// #include "config.h"
// #include <arm/NXP/LPC17xx/LPC17xx.h>
// #include <arm/bits.h>
// #include "iec-bus.h"
// #include "llfl-common.h"
// #include "system.h"
// #include "timer.h"
// #include "fastloader-ll.h"


// static const generic_2bit_t epyxcart_send_def =
// {
//     .pairtimes = {100, 200, 300, 400},
//     .clockbits = {7, 6, 3, 2},
//     .databits  = {5, 4, 1, 0},
//     .eorvalue  = 0xff
// };

// uint8_t epyxcart_send_byte ( uint8_t byte )
// {
//     uint8_t result = 0;

//     llfl_setup();
//     disable_interrupts();

//     /* clear bus */
//     set_data ( 1 );
//     set_clock ( 1 );
//     delay_us ( 3 );

//     /* wait for start signal */
//     llfl_wait_data ( 1, ATNABORT );

//     if ( !IEC_ATN )
//     {
//         result = 1;
//         goto exit;
//     }

//     /* transmit data */
//     llfl_generic_load_2bit ( &epyxcart_send_def, byte );

//     /* data hold time */
//     delay_us ( 20 );

// exit:
//     disable_interrupts();
//     llfl_teardown();
//     return result;
// }
