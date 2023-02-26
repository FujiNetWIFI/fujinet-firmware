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

#ifndef IECDEFINES_H
#define IECDEFINES_H

#include <stdint.h>

// The base pointer of basic.
#define C64_BASIC_START 0x0801

// 1541 RAM and ROM memory map definitions.
#define IEC_1541_RAM_OFFSET 0
#define IEC_1541_RAM_SIZE (1024 * 2)
#define IEC_1541_VIA1_OFFSET 0x1800
#define IEC_1541_VIA1_SIZE 0x10
#define IEC_1541_VIA2_OFFSET 0x1C00
#define IEC_1541_VIA2_SIZE 0x10
#define IEC_1541_ROM_OFFSET 0xC000
#define IEC_1541_ROM_SIZE (1024 * 16)

// Largest Serial byte buffer request from / to.
#define MAX_BYTES_PER_REQUEST 256

// Back arrow character code.
#define IEC_DOLLAR_SIGN '$'
#define IEC_EXCLAMATION_MARKS "!!"

#define IEC_ARROW_LEFT "\x5F"
#define IEC_ARROW_UP "\x5E"
#define IEC_CRSR_LEFT "\x9d"
#define IEC_DEL_DEL "\x14\x14"

#define IEC_HOME "\x13"
#define IEC_CLEAR "\x93"
#define IEC_INSERT "\x94"
#define IEC_DELETE "\x14"
#define IEC_RETURN "\x0D"

#define IEC_CURSOR_DOWN "\x11"
#define IEC_CURSOR_RIGHT "\x1D"
#define IEC_CURSOR_UP "\x91"
#define IEC_CURSOR_LEFT "\x9D"

#define IEC_RUN "\x83"
#define IEC_STOP "\x03"

#define IEC_WHITE "\x05"
#define IEC_RED "\x1C"
#define IEC_GREEN "\x1E"
#define IEC_BLUE "\x1F"
#define IEC_ORANGE "\x81"
#define IEC_BLACK "\x90"
#define IEC_BROWN "\x95"
#define IEC_PINK "\x96"
#define IEC_DARK_GREY "\x97"
#define IEC_GREY "\x98"
#define IEC_LIGHT_GREEN "\x99"
#define IEC_LIGHT_BLUE "\x9A"
#define IEC_LIGHT_GREY "\x9B"
#define IEC_PURPLE "\x9C"
#define IEC_YELLOW "\x9E"
#define IEC_CYAN "\x9F"

#define IEC_REVERSE_ON "\x12"
#define IEC_REVERSE_OFF "\x92"

#define IEC_CS_UPPPER "\x0E"
#define IEC_CS_GFX "\x8E"

// Device OPEN channels.
// Special channels.
enum IECChannels
{
    READ_CHANNEL = 0,
    WRITE_CHANNEL = 1,
    CMD_CHANNEL = 15
};

typedef enum
{
    ErrOK = 0,
    ErrFilesScratched, // Files scratched response, not an error condition.
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
    ErrNoFileGiven,          // The file name was left out of a command or the DOS does not recognize it as such.
                             // Typically, a colon or equal character has been left out of the command
    ErrCommandNotFound = 39, // This error may result if the command sent to command channel (secondary address 15) is unrecognizedby the DOS.
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
    ErrIntro,               // power up message or write attempt with DOS mismatch
    ErrDriveNotReady,       // typically in this emulation could also mean: not supported on this file system.
    ErrSerialComm = 97,     // something went sideways with serial communication to the file server.
    ErrNotImplemented = 98, // The command or specific operation is not yet implemented in this device.
    ErrUnknownError = 99,
    ErrCount
} IOErrorMessage;

// BIT Flags
#define CLEAR            0x0000    // clear all flags
#define CLEAR_LOW        0xFF00    // clear low byte
#define ERROR            (1 << 0)  // if this flag is set, something went wrong
#define ATN_PULLED       (1 << 1)  // might be set by iec_receive
#define EOI_RECVD        (1 << 2)
#define EMPTY_STREAM     (1 << 3)
#define COMMAND_RECVD    (1 << 4)

#define JIFFY_ACTIVE     (1 << 8)
#define JIFFY_LOAD       (1 << 9)
#define DOLPHIN_ACTIVE   (1 << 10)
#define WIC64_ACTIVE     (1 << 11)

// IEC protocol timing consts in microseconds (us)
// IEC-Disected p10-11         // Description              // min    typical    max      // Notes
#define TIMEOUT_Tat    1000    // ATN RESPONSE (REQUIRED)     -      -          1000us      (If maximum time exceeded, device not present error.)
#define TIMING_Th      0       // LISTENER HOLD-OFF           0      -          infinte
#define TIMING_Tne     40      // NON-EOI RESPONSE TO RFD     -      40us       200us       (If maximum time exceeded, EOI response required.)
#define TIMEOUT_Tne    250
#define TIMING_Ts      70      // BIT SET-UP TALKER           20us   70us       -           
#define TIMING_Tv      80      // DATA VALID                  20us   20us       -           (Tv and Tpr minimum must be 60μ s for external device to be a talker. )
#define TIMING_Tf      20      // FRAME HANDSHAKE             0      20us       1000us      (If maximum time exceeded, frame error.)
#define TIMEOUT_Tf     1000
#define TIMING_Tr      20      // FRAME TO RELEASE OF ATN     20us   -          -
#define TIMING_Tbb     100     // BETWEEN BYTES TIME          100us  -          -
#define TIMING_Tye     250     // EOI RESPONSE TIME           200us  250us      -
#define TIMING_Tei     80      // EOI RESPONSE HOLD TIME      60us   -          -           (Tei minimum must be 80μ s for external device to be a listener.)
#define TIMING_Try     30      // TALKER RESPONSE LIMIT       0      30us       60us
#define TIMEOUT_Try    60
#define TIMING_Tpr     60      // BYTE-ACKNOWLEDGE            20us   30us       -           (Tv and Tpr minimum must be 60μ s for external device to be a talker.)
#define TIMING_Ttk     30      // TALK-ATTENTION RELEASE      20us   30us       100us
#define TIMEOUT_Ttk    100
#define TIMING_Tdc     0       // TALK-ATTENTION ACKNOWLEDGE  0      -          -
#define TIMING_Tda     80      // TALK-ATTENTION ACK. HOLD    80us   -          -
#define TIMING_Tfr     60      // EOI ACKNOWLEDGE             60us   -          -

#define TIMING_EMPTY   512     // SIGNAL EMPTY STREAM
#define TIMING_STABLE  60      // WAIT FOR BUS TO BE STABLE

#define TIMING_JIFFY_DETECT   218  // JIFFYDOS ENABLED DELAY ON LAST BIT
#define TIMING_JIFFY_ACK      101  // JIFFYDOS ACK RESPONSE

// See timeoutWait
#define TIMEOUT_DEFAULT 1000 // 1ms
#define TIMED_OUT -1
#define FOREVER 0

#ifndef IEC_INVERTED_LINES
// Not Inverted
#define PULLED    true
#define RELEASED  false
#define LOW 0x00
#define HIGH 0x01
#else
// Inverted
#define PULLED    false
#define RELEASED  true
#define LOW 0x01
#define HIGH 0x00
#endif

#endif // IECDEFINES_H
