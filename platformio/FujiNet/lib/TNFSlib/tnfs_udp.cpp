#include "tnfs_udp.h"

WiFiUDP UDP;
// byte tnfs_fd;
tnfsPacket_t tnfsPacket;
//byte sector[128];
int dataidx = 0;

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
tnfsSessionID_t tnfs_mount(FSImplPtr hostPtr) //(unsigned char hostSlot)
{

  tnfsSessionID_t tempID;

  std::string mp(hostPtr->mountpoint());

  // extract the parameters
  //host + sep + numstr + sep + "0 0 " + location + sep + userid + sep + password;
  char host[36];
  uint16_t port;
  char location[36];
  char userid[36];
  char password[36];
  int n = sscanf(mp.c_str(), "%s %u %*u %*u %s %s %s", host, &port, location, userid, password);

  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));

    // Do not mount, if we already have a session ID, just bail.
    // if (tnfsSessionIDs[hostSlot].session_idl != 0 && tnfsSessionIDs[hostSlot].session_idh != 0)
    //   return true;

    tnfsPacket.session_idl = 0;
    tnfsPacket.session_idh = 0;
    tnfsPacket.retryCount = 0;
    tnfsPacket.command = 0;
    tnfsPacket.data[0] = 0x01; // vers
    tnfsPacket.data[1] = 0x00; // "  "
    // todo: need to strcpy location, userid and password
    tnfsPacket.data[2] = 0x2F; // /
    tnfsPacket.data[3] = 0x00; // nul
    tnfsPacket.data[4] = 0x00; // no username
    tnfsPacket.data[5] = 0x00; // no password

#ifdef DEBUG_VERBOSE
    Debug_print("Mounting / from ");
    Debug_println((char *)hostSlots.host[hostSlot]);
    for (int i = 0; i < 32; i++)
      Debug_printf("%02x ", hostSlots.host[hostSlot][i]);
    Debug_printf("\n\n");
    Debug_print("Req Packet: ");
    for (int i = 0; i < 10; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S */

    UDP.beginPacket(host, port);
    UDP.write(tnfsPacket.rawData, 10);
    UDP.endPacket();

#ifdef DEBUG_VERBOSE
    Debug_println("Wrote the packet");
#endif

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
#ifdef DEBUG_VERBOSE
        Debug_print("Resp Packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif /* DEBUG_S */
        if (tnfsPacket.data[0] == 0x00)
        {
// Successful
#ifdef DEBUG_VERBOSE
          Debug_print("Successful, Session ID: ");
          Debug_print(tnfsPacket.session_idl, HEX);
          Debug_println(tnfsPacket.session_idh, HEX);
#endif /* DEBUG_S */
          // Persist the session ID.
          tempID.session_idl = tnfsPacket.session_idl;
          tempID.session_idh = tnfsPacket.session_idh;
          return tempID;
        }
        else
        {
// Error
#ifdef DEBUG_VERBOSE
          Debug_print("Error #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
          tempID.session_idh = 0;
          tempID.session_idl = 0;
          return tempID;
        }
      }
    }
// Otherwise we timed out.
#ifdef DEBUG_VERBOSE
    Debug_println("Timeout after 5000ms");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
  tempID.session_idh = 0;
  tempID.session_idl = 0;
  return tempID;
}

/*
----------------------------------
OPEN - Opens a file - Command 0x29
----------------------------------
Format: Standard header, flags, mode, then the null terminated filename.
Flags are a bit field.

The flags are:
O_RDONLY        0x0001  Open read only
O_WRONLY        0x0002  Open write only
O_RDWR          0x0003  Open read/write
O_APPEND        0x0008  Append to the file, if it exists (write only)
O_CREAT         0x0100  Create the file if it doesn't exist (write only)
O_TRUNC         0x0200  Truncate the file on open for writing
O_EXCL          0x0400  With O_CREAT, returns an error if the file exists

The modes are the same as described by CHMOD (i.e. POSIX modes). These
may be modified by the server process's umask. The mode only applies
when files are created (if the O_CREAT flag is specified)

Examples: 
Open a file called "/foo/bar/baz.bas" for reading:

0xBEEF 0x00 0x29 0x0001 0x0000 /foo/bar/baz.bas 0x00

Open a file called "/tmp/foo.dat" for writing, creating the file but
returning an error if it exists. Modes set are S_IRUSR, S_IWUSR, S_IRGRP
and S_IWOTH (read/write for owner, read-only for group, read-only for
others):

0xBEEF 0x00 0x29 0x0102 0x01A4 /tmp/foo.dat 0x00

The server returns the standard header and a result code in response.
If the operation was successful, the byte following the result code
is the file descriptor:

0xBEEF 0x00 0x29 0x00 0x04 - Successful file open, file descriptor = 4
0xBEEF 0x00 0x29 0x01 - File open failed with "permssion denied"

(HISTORICAL NOTE: OPEN used to have command id 0x20, but with the
addition of extra flags, the id was changed so that servers could
support both the old style OPEN and the new OPEN)
 */

//bool tnfs_open(unsigned char deviceSlot, unsigned char options, bool create)
int tnfs_open(TNFSImpl* F, const char *mountPath, byte flag_lsb, byte flag_msb)
{

  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  int c = 0;
  unsigned char retries = 0;

  while (retries < 5)
  {
    //strcpy(mountPath, deviceSlots.slot[deviceSlot].file);
    //tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    //tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.retryCount++;   // increase sequence #
    tnfsPacket.command = 0x29; // OPEN

    // if (options == 0x01)
    //   tnfsPacket.data[c++] = 0x01;
    // else if (options == 0x02)
    //   tnfsPacket.data[c++] = 0x03;
    // else
    //   tnfsPacket.data[c++] = 0x03;
    tnfsPacket.data[c++] = flag_lsb;

    // tnfsPacket.data[c++] = (create == true ? 0x01 : 0x00); // Create flag
    tnfsPacket.data[c++] = flag_msb;

    tnfsPacket.data[c++] = 0xFF; // mode
    tnfsPacket.data[c++] = 0x01; // 0777
    // tnfsPacket.data[c++] = '/'; // Filename start

    for (int i = 0; i < strlen(mountPath); i++)
    {
      tnfsPacket.data[i + 5] = mountPath[i];
      c++;
    }

    tnfsPacket.data[c++] = 0x00;
    tnfsPacket.data[c++] = 0x00;
    tnfsPacket.data[c++] = 0x00;

#ifdef DEBUG_VERBOSE
    Debug_printf("Opening /%s\n", mountPath);
    Debug_println("");
    Debug_print("Req Packet: ");
    for (int i = 0; i < c + 4; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
#endif /* DEBUG_S */

    //UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, c + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif // DEBUG_S
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          //tnfs_fds[deviceSlot] = tnfsPacket.data[1];
          int fid = tnfsPacket.data[1];
          return fid;
#ifdef DEBUG_VERBOSE
          Debug_print("Successful, file descriptor: #");
          Debug_println(tnfs_fds[deviceSlot], HEX);
#endif /* DEBUG_S */
          return true;
        }
        else
        {
// unsuccessful
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return -1;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("Failed\n");
#endif
  return -1;
}

/*
------------------------------------
CLOSE - Closes a file - Command 0x23
------------------------------------
  Closes an open file. Consists of the standard header, followed by
  the file descriptor. Example:

  0xBEEF 0x00 0x23 0x04 - Close file descriptor 4

  The server replies with the standard header followed by the return
  code:

  0xBEEF 0x00 0x23 0x00 - File closed.
  0xBEEF 0x00 0x23 0x06 - Operation failed with EBADF, "bad file descriptor"
*/
//bool tnfs_close(unsigned char deviceSlot)
bool tnfs_close(TNFSImpl *F, byte fd, const char *mountPath)
{
  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  int c = 0;
  unsigned char retries = 0;

  while (retries < 5)
  {
    //strcpy(mountPath, deviceSlots.slot[deviceSlot].file);
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.retryCount++;   // increase sequence #
    tnfsPacket.command = 0x23; // CLOSE

    //tnfsPacket.data[c++] = tnfs_fds[deviceSlot];
tnfsPacket.data[c++] = fd;

    for (int i = 0; i < strlen(mountPath); i++)
    {
      tnfsPacket.data[i + 5] = mountPath[i];
      c++;
    }

    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, c + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif // DEBUG_S
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          return true;
        }
        else
        {
// unsuccessful
#ifdef DEBUG_VERBOSE
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("Failed\n");
#endif
  return false;
}

/*
--------------------------------------------------------
OPENDIR - Open a directory for reading - Command ID 0x10
--------------------------------------------------------

  Format:
  Standard header followed by a null terminated absolute path.
  The path delimiter is always a "/". Servers whose underlying 
  file system uses other delimiters, such as Acorn ADFS, should 
  translate. Note that any recent version of Windows understands "/" 
  to be a path delimiter, so a Windows server does not need
  to translate a "/" to a "\".
  Clients should keep track of their own current working directory.

  Example:
  0xBEEF 0x00 0x10 /home/tnfs 0x00 - Open absolute path "/home/tnfs"

  The server responds with the standard header, with byte 4 set to the
  return code which is 0x00 for success, and if successful, byte 5 
  is set to the directory handle.

  Example:
  0xBEEF 0x00 0x10 0x00 0x04 - Successful, handle is 0x04
  0xBEEF 0x00 0x10 0x1F - Failed with code 0x1F
*/
bool tnfs_opendir(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[hostSlot].session_idh;
    tnfsPacket.retryCount++;   // increase sequence #
    tnfsPacket.command = 0x10; // OPENDIR
    tnfsPacket.data[0] = '/';  // Open root dir
    tnfsPacket.data[1] = 0x00; // nul terminated

#ifdef DEBUG
    Debug_println("TNFS Open directory /");
#endif

    UDP.beginPacket(String(hostSlots.host[hostSlot]).c_str(), 16384);
    UDP.write(tnfsPacket.rawData, 2 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          tnfs_dir_fds[hostSlot] = tnfsPacket.data[1];
#ifdef DEBUG_VERBOSE
          Debug_printf("Opened dir on slot #%d - fd = %02x\n", hostSlot, tnfs_dir_fds[hostSlot]);
#endif
          return true;
        }
        else
        {
          // Unsuccessful
          return false;
        }
      }
    }
// Otherwise, we timed out.
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.");
#endif
  return false;
}

/**
---------------------------------------------------
READDIR - Reads a directory entry - Command ID 0x11
---------------------------------------------------

  Format:
  Standard header plus directory handle.

  Example:
  0xBEEF 0x00 0x11 0x04 - Read an entry with directory handle 0x04

  The server responds with the standard header, followed by the directory
  entry. Example:

  0xBEEF 0x17 0x11 0x00 . 0x00 - Directory entry for the current working directory
  0xBEEF 0x18 0x11 0x00 .. 0x00 - Directory entry for parent
  0xBEEF 0x19 0x11 0x00 foo 0x00 - File named "foo"

  If the end of directory is reached, or another error occurs, then the
  status byte is set to the error number as for other commands.
  0xBEEF 0x1A 0x11 0x21 - EOF
  0xBEEF 0x1B 0x11 0x1F - Error code 0x1F
*/
bool tnfs_readdir(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[hostSlot].session_idh;
    tnfsPacket.retryCount++;                     // increase sequence #
    tnfsPacket.command = 0x11;                   // READDIR
    tnfsPacket.data[0] = tnfs_dir_fds[hostSlot]; // Open root dir

#ifdef DEBUG_VERBOSE
    Debug_printf("TNFS Read next dir entry, slot #%d - fd %02x\n\n", hostSlot, tnfs_dir_fds[hostSlot]);
#endif

    UDP.beginPacket(String(hostSlots.host[hostSlot]).c_str(), 16384);
    UDP.write(tnfsPacket.rawData, 1 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          strcpy((char *)&current_entry, (char *)&tnfsPacket.data[1]);
          return true;
        }
        else
        {
          // Unsuccessful
          return false;
        }
      }
    }
// Otherwise, we timed out.
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
}

/**
-----------------------------------------------------
CLOSEDIR - Close a directory handle - Command ID 0x12
-----------------------------------------------------

  Format:
  Standard header plus directory handle.

  Example, closing handle 0x04:
  0xBEEF 0x00 0x12 0x04

  The server responds with the standard header, with byte 4 set to the
  return code which is 0x00 for success, or something else for an error.
  Example:
  0xBEEF 0x00 0x12 0x00 - Close operation succeeded.
  0xBEEF 0x00 0x12 0x1F - Close failed with error code 0x1F
*/
bool tnfs_closedir(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[hostSlot].session_idh;
    tnfsPacket.retryCount++;                     // increase sequence #
    tnfsPacket.command = 0x12;                   // CLOSEDIR
    tnfsPacket.data[0] = tnfs_dir_fds[hostSlot]; // Open root dir

#ifdef DEBUG_VERBOSE
    Debug_println("TNFS dir close");
#endif

    UDP.beginPacket(hostSlots.host[hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 1 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          return true;
        }
        else
        {
          // Unsuccessful
          return false;
        }
      }
    }
// Otherwise, we timed out.
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    retries++;
    tnfsPacket.retryCount--;
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
  return false;
}

/*
---------------------------------------
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

bool tnfs_write(TNFSImpl* F, byte fd, const uint8_t* buf, unsigned short len)
{
  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.retryCount++;                   // Increase sequence
    tnfsPacket.command = 0x22;                 // READ
    tnfsPacket.data[0] = fd; // returned file descriptor
    tnfsPacket.data[1] = len & 0xFF;
    tnfsPacket.data[2] = len >> 8;

#ifdef DEBUG_VERBOSE
    Debug_print("Writing to File descriptor: ");
    Debug_println(tnfs_fds[deviceSlot]);
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S */

    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.write(sector, len);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif /* DEBUG_S */
        if (tnfsPacket.data[0] == 0x00)
        {
// Successful
#ifdef DEBUG_VERBOSE
          Debug_println("Successful.");
#endif /* DEBUG_S */
          return true;
        }
        else
        {
// Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
}

/*
---------------------------------------
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

bool tnfs_read(unsigned char deviceSlot, unsigned short len)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.retryCount++;                   // Increase sequence
    tnfsPacket.command = 0x21;                 // READ
    tnfsPacket.data[0] = tnfs_fds[deviceSlot]; // returned file descriptor
    tnfsPacket.data[1] = len & 0xFF;           // len bytes
    tnfsPacket.data[2] = len >> 8;             //

#ifdef DEBUG_VERBOSE
    Debug_print("Reading from File descriptor: ");
    Debug_println(tnfs_fds[deviceSlot]);
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S */

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.endPacket();
    start = millis();
    dur = millis() - start;
    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif /* DEBUG_S */
        if (tnfsPacket.data[0] == 0x00)
        {
// Successful
#ifdef DEBUG_VERBOSE
          Debug_println("Successful.");
#endif /* DEBUG_S */
          return true;
        }
        else
        {
// Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    if (retries < 5)
      Debug_printf("Retrying...\n");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
  return false;
}

/*
--------------------------------------------------------
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
bool tnfs_seek(unsigned char deviceSlot, long offset)
{
  int start = millis();
  int dur = millis() - start;
  byte offsetVal[4];
  unsigned char retries = 0;

  while (retries < 5)
  {
    offsetVal[0] = (int)((offset & 0xFF000000) >> 24);
    offsetVal[1] = (int)((offset & 0x00FF0000) >> 16);
    offsetVal[2] = (int)((offset & 0x0000FF00) >> 8);
    offsetVal[3] = (int)((offset & 0X000000FF));

    tnfsPacket.retryCount++;
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.command = 0x25; // LSEEK
    tnfsPacket.data[0] = tnfs_fds[deviceSlot];
    tnfsPacket.data[1] = 0x00; // SEEK_SET
    tnfsPacket.data[2] = offsetVal[3];
    tnfsPacket.data[3] = offsetVal[2];
    tnfsPacket.data[4] = offsetVal[1];
    tnfsPacket.data[5] = offsetVal[0];
#ifdef DEBUG
    Debug_print("Seek requested to offset: ");
    Debug_println(offset);
#endif /* DEBUG */
#ifdef DEBUG_VERBOSE
    Debug_print("Req packet: ");
    for (int i = 0; i < 10; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S*/

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 6 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif /* DEBUG_S */

        if (tnfsPacket.data[0] == 0)
        {
// Success.
#ifdef DEBUG_VERBOSE
          Debug_println("Successful.");
#endif /* DEBUG_S */
          return true;
        }
        else
        {
// Error.
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    if (retries < 5)
      Debug_printf("Retrying...\n");
#endif /* DEBUG_S */
    tnfsPacket.retryCount--;
    retries++;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
  return false;
}

// CAN THIS BE MADE USING FS CALLS INSTEAD? THEN IT WILL WORK FOR EVERY FS.
/**
   TNFS Write blank ATR
/
bool tnfs_write_blank_atr(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors)
{
  unsigned long num_para = num_sectors_to_para(numSectors, sectorSize);
  unsigned long offset;

  // Write header
  atrHeader.magic1 = 0x96;
  atrHeader.magic2 = 0x02;
  atrHeader.filesizeH = num_para & 0xFF;
  atrHeader.filesizeL = (num_para & 0xFF00) >> 8;
  atrHeader.filesizeHH = (num_para & 0xFF0000) >> 16;
  atrHeader.secsizeH = sectorSize & 0xFF;
  atrHeader.secsizeL = sectorSize >> 8;

#ifdef DEBUG
  Debug_printf("TNFS: Write header\n");
#endif
  memcpy(sector, atrHeader.rawData, sizeof(atrHeader.rawData));
  tnfs_write(deviceSlot, sizeof(atrHeader.rawData));
  offset += sizeof(atrHeader.rawData);

  // Write first three 128 byte sectors
  memset(sector, 0x00, sizeof(sector));

#ifdef DEBUG
  Debug_printf("TNFS: Write first three sectors\n");
#endif

  for (unsigned char i = 0; i < 3; i++)
  {
    tnfs_write(deviceSlot, 128);
    offset += 128;
    numSectors--;
  }

#ifdef DEBUG
  Debug_printf("TNFS: Sparse Write the rest.\n");
#endif
  // Write the rest of the sectors via sparse seek
  offset += (numSectors * sectorSize) - sectorSize;
  tnfs_seek(deviceSlot, offset);
  tnfs_write(deviceSlot, sectorSize);
  return true; //fixme
}

*/