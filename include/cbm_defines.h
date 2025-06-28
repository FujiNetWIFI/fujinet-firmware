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


#ifndef CBMDEFINES_H
#define CBMDEFINES_H

#include <cstdint>

// The base pointer of basic.
#define CBM_BASIC_START     0x0401

// 1541 RAM and ROM memory map definitions.
#define CBM_1541_RAM_OFFSET 0
#define CBM_1541_RAM_SIZE  (1024 * 2)
#define CBM_1541_VIA1_OFFSET 0x1800
#define CBM_1541_VIA1_SIZE 0x10
#define CBM_1541_VIA2_OFFSET 0x1C00
#define CBM_1541_VIA2_SIZE 0x10
#define CBM_1541_ROM_OFFSET 0xC000
#define CBM_1541_ROM_SIZE (1024 * 16)

// Back arrow character code.
#define CBM_DOLLAR_SIGN '$'
#define CBM_EXCLAMATION_MARKS "!!"

#define CBM_ARROW_LEFT "\x5F"
#define CBM_ARROW_UP "\x5E"
#define CBM_CRSR_LEFT "\x9d"
#define CBM_DEL_DEL "\x14\x14"

#define CBM_HOME "\x13"
#define CBM_CLEAR "\x93"
#define CBM_INSERT "\x94"
#define CBM_DELETE "\x14"
#define CBM_RETURN "\x0D"

#define CBM_CURSOR_DOWN "\x11"
#define CBM_CURSOR_RIGHT "\x1D"
#define CBM_CURSOR_UP "\x91"
#define CBM_CURSOR_LEFT "\x9D"

#define CBM_RUN "\x83"
#define CBM_STOP "\x03"

#define CBM_WHITE "\x05"
#define CBM_RED "\x1C"
#define CBM_GREEN "\x1E"
#define CBM_BLUE "\x1F"
#define CBM_ORANGE "\x81"
#define CBM_BLACK "\x90"
#define CBM_BROWN "\x95"
#define CBM_PINK "\x96"
#define CBM_DARK_GREY "\x97"
#define CBM_GREY "\x98"
#define CBM_LIGHT_GREEN "\x99"
#define CBM_LIGHT_BLUE "\x9A"
#define CBM_LIGHT_GREY "\x9B"
#define CBM_PURPLE "\x9C"
#define CBM_YELLOW "\x9E"
#define CBM_CYAN "\x9F"

#define CBM_REVERSE_ON "\x12"
#define CBM_REVERSE_OFF "\x92"

#define CBM_CS_UPPPER "\x0E"
#define CBM_CS_GFX "\x8E"

#define CBM_SCREEN_ROWS 25
#define CBM_SCREEN_COLS 40

// Device OPEN channels.
// Special channels.
enum IECChannels
{
    CHANNEL_LOAD = 0,
    CHANNEL_SAVE = 1,
    CHANNEL_COMMAND = 15
};

typedef enum
{
    ErrOK = 0,
    ErrFilesScratched,              // Files scratched response, not an error condition.
    ErrBlockHeaderNotFound = 20,
    ErrSyncCharNotFound,
    ErrDataBlockNotFound,
    ErrChecksumInData,
    ErrByteDecoding,
    ErrWriteVerify,
    ErrWriteProtectOn,
    ErrChecksumInHeader,
    ErrDataExtendsNextBlock,
    ErrDiskIdMismatch,
    ErrSyntaxError,
    ErrInvalidCommand,
    ErrLongLine,
    ErrInvalidFilename,
    ErrNoFileGiven,                 // The file name was left out of a command or the DOS does not recognize it as such.
                                    // Typically, a colon or equal character has been left out of the command
    ErrCommandNotFound = 39,        // This error may result if the command sent to command channel (secondary address 15) is unrecognizedby the DOS.
    ErrRecordNotPresent = 50,
    ErrOverflowInRecord,
    ErrFileTooLarge,
    ErrFileOpenForWrite = 60,
    ErrFileNotOpen,
    ErrFileNotFound,
    ErrFileExists,
    ErrFileTypeMismatch,
    ErrNoBlock,
    ErrIllegalTrackOrSector,
    ErrIllegalSystemTrackOrSector,
    ErrNoChannelAvailable = 70,
    ErrDirectoryError,
    ErrDiskFullOrDirectoryFull,
    ErrIntro,                       // power up message or write attempt with DOS mismatch
    ErrDriveNotReady,               // typically in this emulation could also mean: not supported on this file system.
    ErrSerialComm = 97,             // something went sideways with serial communication to the file server.
    ErrNotImplemented = 98,         // The command or specific operation is not yet implemented in this device.
    ErrUnknownError = 99,
    ErrCount
} IOErrorMessage;


// BIT Flags
#define CLEAR            0x0000    // clear all flags
#define CLEAR_LOW        0xFF00    // clear low byte
#define ERROR            (1 << 0)  // if this flag is set, something went wrong
#define ATN_ASSERTED     (1 << 1)  // might be set by iec_receive
#define EOI_RECVD        (1 << 2)
#define EMPTY_STREAM     (1 << 3)

// Detected Protocols
#define FAST_SERIAL_ACTIVE  (1 << 8)
#define PARALLEL_ACTIVE     (1 << 9)
#define SAUCEDOS_ACTIVE     (1 << 10)
#define JIFFYDOS_ACTIVE     (1 << 11)
#define WIC64_ACTIVE        (1 << 12)

// IEC protocol timing consts in microseconds (us)
// IEC-Disected p10-11          // Description              //   1541    C64     min     typical     max         // Notes
// TALKER
#define TIMEOUT_Tat     1000    // ATN RESPONSE (REQUIRED)                       -       -           1000us      (If maximum time exceeded, device not present error.)
#define TIMING_Tne      55      // NON-EOI RESPONSE TO RFD                       -       40us        200us       (If maximum time exceeded, EOI response required.)
#define TIMING_Tna      32      // Extra delay before first bit is sent
#define TIMEOUT_Tne     250

#define TIMING_Ts       80      // BIT SET-UP TALKER                     71us    20us    70us        -           
#define TIMING_Ts0      40      // BIT SET-UP LISTENER PRE       57us    47us
#define TIMING_Ts1      30      // BIT SET-UP LISTENER POST      18us    24us
#define TIMING_Tv       20      // DATA VALID VIC20              76us    26us    20us    20us        -           (Tv and Tpr minimum must be 60μ s for external device to be a talker. )
#define TIMING_Tv64     60      // DATA VALID C64

#define TIMING_Tr       20      // FRAME TO RELEASE OF ATN                       20us    -           -
#define TIMING_Tbb      100     // BETWEEN BYTES TIME                            100us   -           -
#define TIMING_Tye      250     // EOI RESPONSE TIME                             200us   250us       -

#define TIMING_Try      30      // TALKER RESPONSE LIMIT                         0       30us        60us
#define TIMEOUT_Try     60

// LISTENER
#define TIMING_Th       60      // LISTENER HOLD-OFF             65us    39us    0       -           infinte
#define TIMING_Tf       20      // FRAME HANDSHAKE                               0       20us        1000us      (If maximum time exceeded, frame error.)
#define TIMEOUT_Tf      1000

#define TIMING_Tei      80      // EOI RESPONSE HOLD TIME                        60us    -           -           (Tei minimum must be 80μ s for external device to be a listener.)
#define TIMING_Tpr      30      // BYTE-ACKNOWLEDGE                              20us    30us        -           (Tv and Tpr minimum must be 60μ s for external device to be a talker.)
#define TIMING_Ttk      20      // TALK-ATTENTION RELEASE        20us            20us    30us        100us
#define TIMEOUT_Ttk     100
#define TIMING_Tdc      20      // TALK-ATTENTION ACKNOWLEDGE    20us            0       -           -
#define TIMING_Tda      80      // TALK-ATTENTION ACK. HOLD                      80us    -           -
#define TIMING_Tfr      60      // EOI ACKNOWLEDGE                               60us    -           -

// OTHER
#define TIMING_EMPTY    512     // SIGNAL EMPTY STREAM
#define TIMEOUT_ATNCLK  70      // WAIT FOR CLK AFTER ATN IS ASSERTED
#define TIMEOUT_Ttlta   65      // TALKER/LISTENER TURNAROUND TIMEOUT
#define TIMING_Ttca     13      // TALKER TURNAROUND CLOCK ASSERT

// SPECIAL
#define TIMING_PROTOCOL_DETECT   200  // SAUCEDOS/JIFFYDOS CAPABLE DELAY
#define TIMING_PROTOCOL_ACK      80   // SAUCEDOS/JIFFYDOS ACK RESPONSE

// See timeoutWait
#define TIMEOUT_DEFAULT 1000 // 1ms
#define TIMED_OUT -1
#define FOREVER 5000000 // 0

#ifndef IEC_INVERTED_LINES
// Not Inverted
#define IEC_ASSERTED  true
#define IEC_RELEASED  false
#else
// Inverted
#define IEC_ASSERTED  false
#define IEC_RELEASED  true
#endif

#endif // CBMDEFINES_H
