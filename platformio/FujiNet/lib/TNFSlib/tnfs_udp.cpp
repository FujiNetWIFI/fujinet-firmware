#include "tnfs_udp.h"

WiFiUDP UDP;
// byte tnfs_fd;
tnfsPacket_t tnfsPacket;
//byte sector[128];
int dataidx = 0;

void str2packet(String S)
{
  for (int i = 0; i < S.length(); i++)
  {
    dataidx++;
    tnfsPacket.data[dataidx] = S.charAt(i);
  }
  dataidx++;
  tnfsPacket.data[dataidx] = 0; // null terminator
};

/* 
------------------------------------------------------------------
MOUNT - Command ID 0x00
-----------------------
Format:
Standard header followed by:
Bytes 4+: 16 bit version number, little endian, LSB = minor, MSB = major
          NULL terminated string: mount location
          NULL terminated string: user id (optional - NULL if no user id)
          NULL terminated string: password (optional - NULL if no passwd)

The server responds with the standard header:
Bytes 0,1       Connection ID (ignored for client's "mount" command)
Byte  2         Retry number
Byte  3         Command
If the operation was successful, the standard header contains the session number.
Byte 4 contains the command or error code. Then there is the
TNFS protocol version that the server is using following the header, followed by the 
minimum retry time in milliseconds as a little-endian 16 bit number.

Return cases:
true - successful mount.
false with error code in tnfsPacket.data[0] 
false with zero in tnfsPacket.data[0] - timeout
------------------------------------------------------------------
*/
bool tnfs_mount(String host, int port, String location, String userid, String password)
{
  int start = millis();
  int dur = millis() - start;

  memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));
  tnfsPacket.session_idl = 0;
  tnfsPacket.session_idh = 0;
  tnfsPacket.retryCount = 0;
  tnfsPacket.command = 0;    // MOUNT command code
  tnfsPacket.data[0] = 0x01; // vers LSB
  tnfsPacket.data[1] = 0x00; // vers MSB
  dataidx = 1;
  str2packet(location);
  str2packet(userid);
  str2packet(password);
  dataidx += 5;
#ifdef DEBUG_S
  BUG_UART.print("Mounting / from ");
  BUG_UART.println(host);
  BUG_UART.print("Req Packet: ");
  for (int i = 0; i < dataidx; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(host.c_str(), port);
  UDP.write(tnfsPacket.rawData, dataidx);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
#ifdef DEBUG_S
      BUG_UART.print("Resp Packet: ");
      for (int i = 0; i < l; i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
#ifdef DEBUG_S
        BUG_UART.print("Successful, Session ID: ");
        BUG_UART.print(tnfsPacket.session_idl, HEX);
        BUG_UART.println(tnfsPacket.session_idh, HEX);
#endif /* DEBUG_S */
        return true;
      }
      else
      {
        // Error
#ifdef DEBUG_S
        BUG_UART.print("Error #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
        return false;
      }
    }
  }
  // Otherwise we timed out.
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms");
#endif /* DEBUG_S */
  return false;
}

/*
OPEN - Opens a file - Command 0x29
----------------------------------
Format: Standard header, flags, mode, then the null terminated filename.


WIll not implement CHMOD mode - default to something for O_CREAT.

The server returns the standard header and a result code in response.
If the operation was successful, the byte following the result code
is the file descriptor:

0xBEEF 0x00 0x29 0x00 0x04 - Successful file open, file descriptor = 4
0xBEEF 0x00 0x29 0x01 - File open failed with "permssion denied"
 */
int tnfs_open(String host, int port, String filename, byte flag_lsb, byte flag_msb)
{ // need to return file descriptor tnfs_fd and error code. Hmmmm. maybe error code is negative.
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;   // increase sequence #
  tnfsPacket.command = 0x29; // OPEN
  tnfsPacket.data[0] = flag_lsb;
  tnfsPacket.data[1] = flag_msb;
  tnfsPacket.data[2] = 0x00; // chmod
  tnfsPacket.data[3] = 0x00; //
  dataidx = 3;
  str2packet(filename);
  dataidx += 5;

#ifdef DEBUG_S
  BUG_UART.print("Opening ");
  BUG_UART.print(filename);
  BUG_UART.print(" at ");
  BUG_UART.print(host);
  BUG_UART.print(" on port ");
  BUG_UART.println(port, DEC);
  BUG_UART.print("Req packet: ");
  for (int i = 0; i < dataidx; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(host.c_str(), port);
  UDP.write(tnfsPacket.rawData, dataidx);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
#ifdef DEBUG_S
        BUG_UART.print("Successful, file descriptor: #");
        BUG_UART.println(tnfsPacket.data[1], HEX);
#endif /* DEBUG_S */
        return tnfsPacket.data[1];
      }
      else
      {
        // unsuccessful
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return -tnfsPacket.data[0];
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
  return -0x30;
}

/*
READ - Reads from a file - Command 0x21
---------------------------------------
Reads a block of data from a file. Consists of the standard header
followed by the file descriptor as returned by OPEN, then a 16 bit
little endian integer specifying the size of data that is requested.

The server will only reply with as much data as fits in the maximum
TNFS datagram size of 1K when using UDP as a transport. For the
TCP transport, sequencing and buffering etc. are just left up to
the TCP stack, so a READ operation can return blocks of up to 64K. 

If there is less than the size requested remaining in the file, 
the server will return the remainder of the file.  Subsequent READ 
commands will return the code EOF.

Examples:
Read from fd 4, maximum 256 bytes:

0xBEEF 0x00 0x21 0x04 0x00 0x01

The server will reply with the standard header, followed by the single
byte return code, the actual amount of bytes read as a 16 bit unsigned
little endian value, then the data, for example, 256 bytes:

0xBEEF 0x00 0x21 0x00 0x00 0x01 ...data...

End-of-file reached:

0xBEEF 0x00 0x21 0x21
*/
int tnfs_read(String host, int port, byte fd, size_t size)
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;                // Increase sequence
  tnfsPacket.command = 0x21;              // READ
  tnfsPacket.data[0] = fd;                // returned file descriptor
  tnfsPacket.data[1] = byte(size & 0xff); // size lsb
  tnfsPacket.data[2] = byte(size >> 8);   // size msb

#ifdef DEBUG_S
  BUG_UART.print("Reading from File descriptor: ");
  BUG_UART.println(fd);
  BUG_UART.print("Req Packet: ");
  for (int i = 0; i < 7; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(host.c_str(), port);
  UDP.write(tnfsPacket.rawData, 4 + 3);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
#ifndef DEBUG_S
        BUG_UART.println("Successful.");
#endif /* DEBUG_S */
        return int(size);
      }
      else
      {
        // Error
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return -1;
      }
    }
  }
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
  return -1;
}

/*
WRITE - Writes to a file - Command 0x22
---------------------------------------
Writes a block of data to a file. Consists of the standard header,
followed by the file descriptor, followed by a 16 bit little endian
value containing the size of the data, followed by the data. The
entire message must fit in a single datagram.

Examples:
Write to fd 4, 256 bytes of data:

0xBEEF 0x00 0x22 0x04 0x00 0x01 ...data...

The server replies with the standard header, followed by the return
code, and the number of bytes actually written. For example:

0xBEEF 0x00 0x22 0x00 0x00 0x01 - Successful write of 256 bytes
0xBEEF 0x00 0x22 0x06 - Failed write, error is "bad file descriptor"
*/

void tnfs_write(String host, int port, byte fd, const uint8_t *sector, size_t size)
{
  int start=millis();
  int dur=millis()-start;
  tnfsPacket.retryCount++;  // Increase sequence
  tnfsPacket.command=0x22;  // WRITE
  tnfsPacket.data[0]=fd; // returned file descriptor
  tnfsPacket.data[1] = byte(size & 0xff); // size lsb
  tnfsPacket.data[2] = byte(size >> 8);   // size msb

#ifdef DEBUG_S
  BUG_UART.print("Writing to File descriptor: ");
  BUG_UART.println(fd);
  BUG_UART.print("Req Packet: ");
  for (int i=0;i<7;i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(host.c_str(),port);
  UDP.write(tnfsPacket.rawData,4+3);
  UDP.write(sector,size);
  UDP.endPacket();

  while (dur<5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,sizeof(tnfsPacket.rawData));
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i=0;i<l;i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
#ifndef DEBUG_S
        BUG_UART.println("Successful.");
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/        
        return;
      }
    }
  }
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}


/*
LSEEK - Seeks to a new position in a file - Command 0x25
--------------------------------------------------------
Seeks to an absolute position in a file, or a relative offset in a file,
or to the end of a file.
The request consists of the header, followed by the file descriptor,
followed by the seek type (SEEK_SET, SEEK_CUR or SEEK_END), followed
by the position to seek to. The seek position is a signed 32 bit integer,
little endian. (2GB file sizes should be more than enough for 8 bit
systems!)

The seek types are defined as follows:
0x00            SEEK_SET - Go to an absolute position in the file
0x01            SEEK_CUR - Go to a relative offset from the current position
0x02            SEEK_END - Seek to EOF

Example:

File descriptor is 4, type is SEEK_SET, and position is 0xDEADBEEF:
0xBEEF 0x00 0x25 0x04 0x00 0xEF 0xBE 0xAD 0xDE

Note that clients that buffer reads for single-byte reads will have
to make a calculation to implement SEEK_CUR correctly since the server's
file pointer will be wherever the last read block made it end up.
*/
void tnfs_seek(String host, int port, byte fd, uint32_t offset)
{
  int start = millis();
  int dur = millis() - start;
  byte offsetVal[4];

  // This may be sending the bytes in the wrong endian, pls check. Easiest way is to flip the indices.
  offsetVal[0] = (byte)((offset & 0xFF000000U) >> 24);
  offsetVal[1] = (byte)((offset & 0x00FF0000U) >> 16);
  offsetVal[2] = (byte)((offset & 0x0000FF00U) >> 8);
  offsetVal[3] = (byte)((offset & 0X000000FFU));

  tnfsPacket.retryCount++;
  tnfsPacket.command = 0x25; // LSEEK
  tnfsPacket.data[0] = fd;
  tnfsPacket.data[1] = 0x00; // SEEK_SET
  tnfsPacket.data[2] = offsetVal[3];
  tnfsPacket.data[3] = offsetVal[2];
  tnfsPacket.data[4] = offsetVal[1];
  tnfsPacket.data[5] = offsetVal[0];

#ifdef DEBUG_S
  BUG_UART.print("Seek requested to offset: ");
  BUG_UART.println(offset);
  BUG_UART.print("Req packet: ");
  for (int i = 0; i < 10; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S*/

  UDP.beginPacket(host.c_str(), port);
  UDP.write(tnfsPacket.rawData, 6 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */

      if (tnfsPacket.data[0] == 0)
      {
        // Success.
#ifdef DEBUG_S
        BUG_UART.println("Successful.");
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error.
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;
      }
    }
  }
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}
