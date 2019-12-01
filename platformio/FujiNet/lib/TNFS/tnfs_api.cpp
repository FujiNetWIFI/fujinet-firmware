#include "tnfs_api.h"

WiFiUDP UDP;

//TNFSImpl::TNFSImpl() { }

FileImplPtr TNFSImpl::open(const char *path, const char *mode)
{
  tnfs_open(); // what about return pointer?
  return std::make_shared<TNFSFileImpl>(this, path, mode);
}

bool TNFSImpl::exists(const char *path)
{
  //File f = open(path, "r");
  //return (f == true) && !f.isDirectory();
  return false;
}

bool TNFSImpl::rename(const char *pathFrom, const char *pathTo) { return false; }
bool TNFSImpl::remove(const char *path) { return false; }
bool TNFSImpl::mkdir(const char *path) { return false; }
bool TNFSImpl::rmdir(const char *path) { return false; }

TNFSFileImpl::TNFSFileImpl(TNFSImpl *fs, const char *path, const char *mode) {}

size_t TNFSFileImpl::write(const uint8_t *buf, size_t size) {}
size_t TNFSFileImpl::read(uint8_t *buf, size_t size)
{
  tnfs_read();
  for (int i = 0; i < 128; i++)
    buf[i] = tnfsPacket.data[i + 3];
}
void TNFSFileImpl::flush() {}
bool TNFSFileImpl::seek(uint32_t pos, SeekMode mode)
{
  tnfs_seek(pos);
}
size_t TNFSFileImpl::position() const {}
size_t TNFSFileImpl::size() const {}
void TNFSFileImpl::close() {}
const char *TNFSFileImpl::name() const {}
time_t TNFSFileImpl::getLastWrite() {}
boolean TNFSFileImpl::isDirectory(void) {}
FileImplPtr TNFSFileImpl::openNextFile(const char *mode) {}
void TNFSFileImpl::rewindDirectory(void) {}
TNFSFileImpl::operator bool() { return _f != NULL; }

/* Thom's things */

union {
  struct
  {
    byte session_idl;
    byte session_idh;
    byte retryCount;
    byte command;
    byte data[508];
  };
  byte rawData[512];
} tnfsPacket;

byte tnfs_fd;

void tnfs_mount(const char *host, uint16_t port)
{
  int start = millis();
  int dur = millis() - start;

  memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));
  tnfsPacket.session_idl = 0;
  tnfsPacket.session_idh = 0;
  tnfsPacket.retryCount = 0;
  tnfsPacket.command = 0;
  tnfsPacket.data[0] = 0x01; // vers
  tnfsPacket.data[1] = 0x00; // "  "
  tnfsPacket.data[2] = 0x2F; // /
  tnfsPacket.data[3] = 0x00; // nul
  tnfsPacket.data[4] = 0x00; // no username
  tnfsPacket.data[5] = 0x00; // no password

#ifdef DEBUG_S
  BUG_UART.print("Mounting / from ");
  BUG_UART.println(host);
  BUG_UART.print("Req Packet: ");
  for (int i = 0; i < 10; i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(host, port);
  UDP.write(tnfsPacket.rawData, 10);
  UDP.endPacket();

  while (dur < 5000)
  {
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
        return;
      }
      else
      {
        // Error
#ifdef DEBUG_S
        BUG_UART.print("Error #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
        return;
      }
    }
  }
  // Otherwise we timed out.
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms");
#endif /* DEBUG_S */
}

/**
 * Open 'autorun.atr'
 */
void tnfs_open()
{
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
        tnfs_fd = tnfsPacket.data[1];
#ifdef DEBUG_S
        BUG_UART.print("Successful, file descriptor: #");
        BUG_UART.println(tnfs_fd, HEX);
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // unsuccessful
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
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
