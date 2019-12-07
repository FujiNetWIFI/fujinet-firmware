#include "tnfs_udp.h"

#define TNFS_SERVER "x"
#define TNFS_PORT 0

WiFiUDP UDP;
byte tnfs_fd;
tnfsPacket_t tnfsPacket;
int dataidx = 0;

void str2packet(const char *s)
{
  for (int i = 0; i < strlen(s); i++)
  {
    dataidx++;
    tnfsPacket.data[dataidx] = s[i];
    dataidx++;
    tnfsPacket.data[dataidx] = 0; // null terminator
  }
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
bool tnfs_mount(const char *host, uint16_t port, const char *location, const char *userid, const char *password)
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
  dataidx=1;
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

  UDP.beginPacket(host, port);
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
int tnfs_open(const char *filename, byte flag_lsb, byte flag_msb)
{ // need to return file descriptor tnfs_fd and error code. Hmmmm. maybe error code is negative.
  
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;   // increase sequence #
  tnfsPacket.command = 0x29; // OPEN
  tnfsPacket.data[0] = 0x01; // R/O
  tnfsPacket.data[1] = 0x00; //
  tnfsPacket.data[2] = 0x00; // Flags
  tnfsPacket.data[3] = 0x00; //
  tnfsPacket.data[4] = '/';  // Filename start
  tnfsPacket.data[5] = 'a';
  tnfsPacket.data[6] = 'u';
  tnfsPacket.data[7] = 't';
  tnfsPacket.data[8] = 'o';
  tnfsPacket.data[9] = 'r';
  tnfsPacket.data[10] = 'u';
  tnfsPacket.data[11] = 'n';
  tnfsPacket.data[12] = '.';
  tnfsPacket.data[13] = 'a';
  tnfsPacket.data[14] = 't';
  tnfsPacket.data[15] = 'r';
  tnfsPacket.data[16] = 0x00; // NUL terminated
  tnfsPacket.data[17] = 0x00; // no username
  tnfsPacket.data[18] = 0x00; // no password

#ifdef DEBUG_S
  BUG_UART.println("Opening /autorun.atr...");
  BUG_UART.print("Req packet: ");
  for (int i = 0; i < 23; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);
  UDP.write(tnfsPacket.rawData, 19 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
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

void tnfs_read()
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;      // Increase sequence
  tnfsPacket.command = 0x21;    // READ
  tnfsPacket.data[0] = tnfs_fd; // returned file descriptor
  tnfsPacket.data[1] = 0x80;    // 128 bytes
  tnfsPacket.data[2] = 0x00;    //

#ifdef DEBUG_S
  BUG_UART.print("Reading from File descriptor: ");
  BUG_UART.println(tnfs_fd);
  BUG_UART.print("Req Packet: ");
  for (int i = 0; i < 7; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);
  UDP.write(tnfsPacket.rawData, 4 + 3);
  UDP.endPacket();

  while (dur < 5000)
  {
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

void tnfs_seek(uint32_t offset)
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
  tnfsPacket.data[0] = tnfs_fd;
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

  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);
  UDP.write(tnfsPacket.rawData, 6 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
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
