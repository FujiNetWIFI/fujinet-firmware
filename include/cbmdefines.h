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

#include <stdint.h>

// The base pointer of basic.
#define C64_BASIC_START     0x0801

// 1541 RAM and ROM memory map definitions.
#define CBM_1541_RAM_OFFSET 0
#define CBM_1541_RAM_SIZE  (1024 * 2)
#define CBM_1541_VIA1_OFFSET 0x1800
#define CBM_1541_VIA1_SIZE 0x10
#define CBM_1541_VIA2_OFFSET 0x1C00
#define CBM_1541_VIA2_SIZE 0x10
#define CBM_1541_ROM_OFFSET 0xC000
#define CBM_1541_ROM_SIZE (1024 * 16)

// Largest Serial byte buffer request from / to.
#define MAX_BYTES_PER_REQUEST 256

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


namespace CBM
{
    // Device OPEN channels.
    // Special channels.
    enum IECChannels
    {
        READ_CHANNEL = 0,
        WRITE_CHANNEL = 1,
        CMD_CHANNEL = 15
    };

    const uint8_t MAX_CBM_SCREEN_ROWS = 25;
    const uint8_t MAX_CBM_SCREEN_COLS = 40;

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

} // namespace CBM

#endif // CBMDEFINES_H
