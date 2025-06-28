#pragma once

// Meatloaf Serial Transfer Protocol
//
// rx "filename" - receive file.  "filename" can include a path to where the file is to be stored.
// tx "filename" [offset] [length] - transmit file with option to request start from offset and byte range.
// status [rx|tx] - status of last recieve/transmit
// mount "filename" /dev/{device id} - mount the file on specified device
// crc32 "filename" - get crc32 of file to detect change
// ​
// ​The data block would look like the following.  Similar to HTTP Chunked Transfer encoding.
// ```sender
// {size} {checksum}\r\n
// {data}\r\n
// ```receiver
// {status code} {status description}]
// ```
// Receiver would acknowledge with:
// '0 OK'
// '1 RESEND'
// '2+ ERROR'
//
// Transfer stops on ERROR. (any status code greater than 1)
// Checksum will be CRC32 of data.
// Last block size and checksum will be '0'.
//

#include "../ConsoleCommand.h"

namespace ESP32Console::Commands
{
    const ConsoleCommand getRXCommand();

    const ConsoleCommand getTXCommand();
}