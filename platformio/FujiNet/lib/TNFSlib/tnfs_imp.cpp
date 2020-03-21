#include "tnfs_imp.h"

#define DEBUG_VERBOSE

WiFiUDP UDP;

tnfsPacket_t tnfsPacket;

/* File System Implementation */

std::string TNFSImpl::host()
{

  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%s", temp);
    if (n == 1)
    {
      _host = temp;
    }
    else
    {
      _host.clear();
    }
  }
#ifdef DEBUG
  Debug_printf("host: %s\n", _host.c_str());
#endif
  return _host;
}

uint16_t TNFSImpl::port()
{
  if (_mountpoint != NULL)
  {
    uint16_t temp;
    int n = sscanf(_mountpoint, "%*s %hu", &temp);
    if (n == 1)
    {
      _port = temp;
    }
#ifdef DEBUG
    Debug_printf("port: %hu\n", _port);
#endif
  }
  return _port;
}

tnfsSessionID_t TNFSImpl::sid()
{
  if (_mountpoint != NULL)
  {
    byte lo;
    byte hi;
    int n = sscanf(_mountpoint, "%*s %*u %hhu %hhu", &lo, &hi);
    if (n == 2)
    {
      _sid.session_idl = lo;
      _sid.session_idh = hi;
    }
  }
#ifdef DEBUG
  Debug_printf("session id: %hhu %hhu\n", _sid.session_idh, _sid.session_idl);
#endif
  return _sid;
}

std::string TNFSImpl::location()
{
  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%*s %*u %s", temp);
    if (n == 1)
    {
      _location = temp;
    }
    else
    {
      _location.clear();
    }
  }
#ifdef DEBUG
  Debug_printf("location: %s\n", _location.c_str());
#endif
  return _location;
}

std::string TNFSImpl::userid()
{
  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%*s %*u %*s %s", temp);
    if (n == 1)
    {
      _userid = temp;
    }
    else
    {
      _userid.clear();
    }
  }
#ifdef DEBUG
  Debug_printf("userid: %s\n", _userid.c_str());
#endif
  return _userid;
}

std::string TNFSImpl::password()
{
  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%*s %*u %*s %*s %s", temp);
    if (n == 1)
    {
      _password = temp;
    }
    else
    {
      _password.clear();
    }
  }
#ifdef DEBUG
  Debug_printf("password: %s\n", _password.c_str());
#endif
  return _password;
}

FileImplPtr TNFSImpl::open(const char *path, const char *mode)
{
  int fid;
  // TODO: path (filename) checking
#ifdef DEBUG
  Debug_printf("Attempting to open TNFS file: %s\n", path);
#endif
  // translate C++ file mode to TNFS file flags
  uint16_t flag = TNFS_RDONLY;
  byte flag_lsb;
  byte flag_msb;
  if (strlen(mode) == 1)
  {
    switch (mode[0])
    {
    case 'r':
      flag = TNFS_RDONLY;
      break;
    case 'w':
      flag = TNFS_WRONLY | TNFS_CREAT | TNFS_TRUNC;
      break;
    case 'a':
      flag = TNFS_WRONLY | TNFS_CREAT | TNFS_APPEND;
      break;
    default:
      return nullptr;
    }
  }
  else if (strlen(mode) == 2)
  {
    if (mode[1] == '+')
    {
      switch (mode[0])
      {
      case 'r':
        flag = TNFS_RDWR;
        break;
      case 'w':
        flag = TNFS_RDWR | TNFS_CREAT | TNFS_TRUNC;
        break;
      case 'a':
        flag = TNFS_RDWR | TNFS_CREAT | TNFS_APPEND;
        break;
      default:
        return nullptr;
      }
    }
    else
    {
      return nullptr;
    }
  }
  flag_lsb = byte(flag & 0xff);
  flag_msb = byte(flag >> 8);
  Debug_printf("open flags (lo,hi): &%u %u\n", flag_lsb, flag_msb);

  // test if path is directory
  tnfsStat_t stats = tnfs_stat(this, path);
  if (stats.isDir)
  {
#ifdef DEBUG
    Debug_printf("opening directory %s\n", path);
#endif
    fid = tnfs_opendir(this, path);
  }
  else
  {
#ifdef DEBUG
    Debug_printf("opening file %s\n", path);
#endif
    fid = tnfs_open(this, path, flag_lsb, flag_msb);
  }
  if (fid == -1)
  {
    return nullptr;
  }
#ifdef DEBUG
  Debug_printf("FID is %d\n", fid);
#endif
  return std::make_shared<TNFSFileImpl>(this, fid, path, stats);
}

bool TNFSImpl::exists(const char *path)
{
  File f = open(path, "r");
  return (f == true); // && !f.isDirectory()); todo:
}

bool TNFSImpl::rename(const char *pathFrom, const char *pathTo) { return false; }
bool TNFSImpl::remove(const char *path) { return false; }
bool TNFSImpl::mkdir(const char *path) { return false; }
bool TNFSImpl::rmdir(const char *path) { return false; }

/* File Implementation */

TNFSFileImpl::TNFSFileImpl(TNFSImpl *fs, int fid, const char *filename, tnfsStat_t stats)
{
  this->fs = fs;
  this->fid = fid;
  strcpy(fn, filename);
  this->stats = stats;
}

TNFSFileImpl::~TNFSFileImpl()
{
#ifdef DEBUG
  Debug_printf("destructor attempting to close file %s\n", fn);
#endif
  //if (fid >= 0)
  // {
  this->close();
  // }
  // else
  // {
  // Debug_println("not an open file");
  // }
}

size_t TNFSFileImpl::write(const uint8_t *buf, size_t size)
{
#ifdef DEBUG_S
  BUG_UART.println("calling tnfs_write");
#endif
  return tnfs_write(fs, fid, buf, size);
}

size_t TNFSFileImpl::read(uint8_t *buf, size_t size)
{
#ifdef DEBUG_S
  BUG_UART.println("calling tnfs_read");
#endif
  return tnfs_read(fs, fid, buf, size);
}

void TNFSFileImpl::flush() {}

bool TNFSFileImpl::seek(uint32_t pos, SeekMode mode)
{
  tnfs_seek(fs, fid, pos); // implement SeekMode
  return true;
}

void TNFSFileImpl::close()
{
  if (fid >= 0)
  {
    if (stats.isDir)
    {
      Debug_println("closing directory");
      tnfs_closedir(fs, fid);
    }
    else
    {
      Debug_println("closing file");
      tnfs_close(fs, fid);
    }
  }
  else
  {
    Debug_println("real file not open");
  }
  fid = -1;
}

const char *TNFSFileImpl::name() const
{
  return fn;
}

boolean TNFSFileImpl::isDirectory(void)
{
  //tnfsStat_t stats = tnfs_stat(fs, fn);
  return stats.isDir;
}

// not written yet
size_t TNFSFileImpl::position() const
{
  // do I need to keep track while reading, writing and seeking?
  return 0;
}

size_t TNFSFileImpl::size() const
{
  //tnfsStat_t stats = tnfs_stat(fs, fn);
  return stats.fsize;
}

time_t TNFSFileImpl::getLastWrite()
{
  //tnfsStat_t stats = tnfs_stat(fs, fn);
  return stats.mtime;
}

FileImplPtr TNFSFileImpl::openNextFile(const char *mode)
{
  char path[256];
  if (stats.isDir)
  {
    do
    {
      bool ok = tnfs_readdir(fs, fid, path);
      if (!ok)
        return nullptr;
    } while (path[0] == '.');
    // return fs->open(path, "r");
    tnfsStat_t fstats = tnfs_stat(fs, path); // get stats on next file
    // create file pointer without opening file - set FID=-2
    if (fstats.isDir)
    {
      std::string P = std::string(path);
      P.push_back('/');
      strcpy(path, P.c_str());
    }
    return std::make_shared<TNFSFileImpl>(fs, -2, path, fstats);
  }
  return nullptr;
}

void TNFSFileImpl::rewindDirectory(void)
{
  if (tnfs_closedir(fs, fid))
  {
    int id = tnfs_opendir(fs, fn);
    if (id != -1)
      fid = id;
  }
}

TNFSFileImpl::operator bool()
{
  // figure out a way to know if we have an open file
  if (fid != -1)
    return true;
  return false;
}

// TNFS calls

/**
 * Dump TNFS packet to debug port
 **/
void tnfs_debug_packet(unsigned short len)
{
#ifdef DEBUG
  Debug_printf("TNFS Packet, Len: %d\n", len);
  for (unsigned short i = 0; i < len; i++)
    Debug_printf("%02x ", tnfsPacket.rawData[i]);
  Debug_printf("\n");

  Debug_printf("TNFS Return Code: ");
  switch (tnfsPacket.data[0])
  {
  case 0x00:
    Debug_printf("Success.");
    break;
  case 0x01:
    Debug_printf("EPERM: Operation not Permitted.");
    break;
  case 0x02:
    Debug_printf("ENOENT: No such file or directory.");
    break;
  case 0x03:
    Debug_printf("EIO: I/O Error");
    break;
  case 0x04:
    Debug_printf("ENXIO: No such device or address");
    break;
  case 0x05:
    Debug_printf("E2BIG: Argument list too long");
    break;
  case 0x06:
    Debug_printf("EBADF: Bad File Number");
    break;
  case 0x07:
    Debug_printf("EAGAIN: Try Again");
    break;
  case 0x08:
    Debug_printf("ENOMEM: Out of memory");
    break;
  case 0x09:
    Debug_printf("EACCES: Permission denied.");
    break;
  case 0x0A:
    Debug_printf("EBUSY: Device or resource busy.");
    break;
  case 0x0B:
    Debug_printf("EEXIST: File Exists");
    break;
  case 0x0C:
    Debug_printf("ENOTDIR: Is not a directory.");
    break;
  case 0x0D:
    Debug_printf("EISDIR: Is a directory.");
    break;
  case 0x0E:
    Debug_printf("EINVAL: Invalid Argument.");
    break;
  case 0x0F:
    Debug_printf("ENFILE: File table overflow.");
    break;
  case 0x10:
    Debug_printf("EMFILE: Too many open files.");
    break;
  case 0x11:
    Debug_printf("EFBIG: File too large.");
    break;
  case 0x12:
    Debug_printf("ENOSPC: No space left on device.");
    break;
  case 0x13:
    Debug_printf("ESPIPE: Attempt to seek on a FIFO or pipe.");
    break;
  case 0x14:
    Debug_printf("EROFS: Read only filesystem.");
    break;
  case 0x15:
    Debug_printf("ENAMETOOLONG: Filename too long.");
    break;
  case 0x16:
    Debug_printf("ENOSYS: Function not implemented.");
    break;
  case 0x17:
    Debug_printf("ENOTEMPTY: Directory not empty.");
    break;
  case 0x18:
    Debug_printf("ELOOP: Too many symbolic links.");
    break;
  case 0x19:
    Debug_printf("ENODATA: No data available.");
    break;
  case 0x1A:
    Debug_printf("ENOSTR: Out of streams resources.");
    break;
  case 0x1B:
    Debug_printf("EPROTO: Protocol Error");
    break;
  case 0x1C:
    Debug_printf("EBADFD: File descriptor in bad state.");
    break;
  case 0x1D:
    Debug_printf("EUSERS: Too many users.");
    break;
  case 0x1E:
    Debug_printf("ENOBUFS: No buffer space avaialable.");
    break;
  case 0x1F:
    Debug_printf("EALREADY: Operation already in progress.");
    break;
  case 0x20:
    Debug_printf("ESTALE: Stale TNFS handle.");
    break;
  case 0x21:
    Debug_printf("EOF!");
    break;
  case 0xFF:
    Debug_printf("Invalid TNFS Handle");
    break;
  }
  Debug_printf("\n");
#endif /* DEBUG */
}

/**
 * Send constructed TNFS packet, and receive a response, with
 * retries.
 * 
 * tnfsPacket is used from global space.
 * It is expected to extract error code from tnfsPacket.data[0]
 * if the return is true.
 * 
 * const char* host - Hostname
 * unsigned short port - port #
 * int len - Length of raw TNFS packet
 * 
 * returns - true if packet was received, false if no packets
 * were received after all attempts.
 */
bool tnfs_transaction(const char *host, unsigned short port, unsigned short len)
{
  byte retries = 0;
  int dur = 0;
  int start = 0;

  while (retries < TNFS_RETRIES)
  {
    start = millis();

    tnfsPacket.retryCount++;

    tnfs_debug_packet(len);

    // Send packet
    UDP.beginPacket(host, port);
    UDP.write(tnfsPacket.rawData, len + 4);
    UDP.endPacket();

    while (dur < TNFS_TIMEOUT)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        unsigned short l = UDP.read(tnfsPacket.rawData, TNFS_PACKET_SIZE);
        tnfs_debug_packet(l);
        return true;
      }
    }

    // we timed out.
    retries++;
    tnfsPacket.retryCount--; // is this correct?
    start = dur = 0;

#ifdef DEBUG
    Debug_printf("Timeout after %d milliseconds. Retrying\n", TNFS_TIMEOUT);
#endif
  }

  // At this point, we've exhausted all attempts
  // Indicate failure.

#ifdef DEBUG
  Debug_printf("All attempts failed");
#endif

  return false;
}

/*
-------------------------
MOUNT - Command ID 0x00
-------------------------
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
  valid nonzero session ID
  invalid zero session ID
*/
tnfsSessionID_t tnfs_mount(FSImplPtr hostPtr) //(unsigned char hostSlot)
{
  tnfsSessionID_t tempID;
  std::string mp(hostPtr->mountpoint());

  tempID.session_idh = tempID.session_idl = 0;

  // extract the parameters
  //host + sep + numstr + sep + "0 0 " + location + sep + userid + sep + password;
  char host[36];
  uint16_t port;
  char location[36];
  //todo:
  char userid[36];
  char password[36];
  sscanf(mp.c_str(), "%s %hu %*u %*u %s %s %s", host, &port, location, userid, password);
#ifdef DEBUG
  Debug_println("Mounting TNFS Server:");
  Debug_printf("host: %s\n", host);
  Debug_printf("port: %hu\n", port);
  Debug_printf("location: %s\n", location);
  Debug_printf("userid: %s\n", userid);
  Debug_printf("password: %s\n", password);
#endif

  memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));

  tnfsPacket.session_idl = 0;
  tnfsPacket.session_idh = 0;
  tnfsPacket.retryCount = 0;
  tnfsPacket.command = 0;

  tnfsPacket.data[0] = 0x01; // vers
  tnfsPacket.data[1] = 0x00; // "  "
  // todo: need to strcpy location, userid and password
  tnfsPacket.data[2] = 0x2F; // '/'
  tnfsPacket.data[3] = 0x00; // nul
  tnfsPacket.data[4] = 0x00; // no username
  tnfsPacket.data[5] = 0x00; // no password

  Debug_print("Mounting / from ");
  Debug_println(host);

  if (tnfs_transaction(host, port, 6))
  {
    // Got a packet
    if (tnfsPacket.data[0] == 0x00)
    {
      // Successful
      tempID.session_idh = tnfsPacket.session_idh;
      tempID.session_idl = tnfsPacket.session_idl;
    }
  }
  return tempID;
}

/*
------------------------
UMOUNT - Command ID 0x01
------------------------
  Format:
  Standard header only, containing the connection ID to terminate, 0x00 as
  the sequence number, and 0x01 as the command.

  Example:
  To UMOUNT the filesystem mounted with id 0xBEEF:

  0xBEEF 0x00 0x01

  The server responds with the standard header and a return code as byte 4.
  The return code is 0x00 for OK. Example:

  0xBEEF 0x00 0x01 0x00

  On error, byte 4 is set to the error code, for example, for error 0x1F:

  0xBEEF 0x00 0x01 0x1F
*/
bool tnfs_umount(FSImplPtr hostPtr)
{
  // tnfsSessionID_t sessionID = ((TNFSImpl)hostPtr)->sid();
  std::string mp(hostPtr->mountpoint());

  // extract the parameters
  //host + sep + numstr + sep + lo + sep + hi + location + sep + userid + sep + password;
  char host[36];
  uint16_t port;
  byte lo;
  byte hi;
  sscanf(mp.c_str(), "%s %hu %hhu %hhu ", host, &port, &lo, &hi);
#ifdef DEBUG
  Debug_println("UnMounting TNFS Server:");
  Debug_printf("host: %s\n", host);
  Debug_printf("port: %hu\n", port);
  Debug_printf("session id: %hhu %hhu\n", lo, hi);
#endif

  tnfsPacket.session_idl = lo;
  tnfsPacket.session_idh = hi;
  tnfsPacket.retryCount = 0; // reset sequence #
  tnfsPacket.command = 0x01; // UMOUNT

  if (tnfs_transaction(host, port, 0))
  {
    return true;
  }
  else
  {
    return false;
  }
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
int tnfs_open(TNFSImpl *F, const char *mountPath, byte flag_lsb, byte flag_msb)
{
  tnfsSessionID_t sessionID = F->sid();
  int c = 0;

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;

  tnfsPacket.command = 0x29; // OPEN

  tnfsPacket.data[c++] = flag_lsb;
  tnfsPacket.data[c++] = flag_msb;

  if (flag_msb & 0x01)
  {
    tnfsPacket.data[c++] = 0xFF; // mode is
    tnfsPacket.data[c++] = 0x01; // 0777
  }
  else
  {
    tnfsPacket.data[c++] = 0x00; // mode is
    tnfsPacket.data[c++] = 0x00; // not used
  }

  for (int i = 0; i < strlen(mountPath); i++)
  {
    tnfsPacket.data[c++] = mountPath[i];
  }

  tnfsPacket.data[c++] = 0x00;
  tnfsPacket.data[c++] = 0x00;
  tnfsPacket.data[c++] = 0x00;

#ifdef DEBUG
  Debug_printf("Opening %s\n", mountPath);
#endif

  if (tnfs_transaction(F->host().c_str(), F->port(), c))
  {
    if (tnfsPacket.data[0] == 0x00)
    {
      // Successful
      return tnfsPacket.data[1]; // Return the file ID.
    }
  }
  return -1; // not successful
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
bool tnfs_close(TNFSImpl *F, int fid)
{
  tnfsSessionID_t sessionID = F->sid();
  int c = 0;

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;

  tnfsPacket.command = 0x23; // CLOSE

  tnfsPacket.data[c++] = (byte)fid;

  return tnfs_transaction(F->host().c_str(), F->port(), c);
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
int tnfs_opendir(TNFSImpl *F, const char *dirName)
{
  tnfsSessionID_t sessionID = F->sid();

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;

  tnfsPacket.command = 0x10; // OPENDIR

  if (dirName[0] != '/')
  {
    tnfsPacket.data[0] = '/';
    strcpy((char *)&tnfsPacket.data[1], dirName);
  }
  else
    strcpy((char *)&tnfsPacket.data[0], dirName);

#ifdef DEBUG
  Debug_print("TNFS Open directory: ");
  Debug_println(dirName);
#endif

  if (tnfs_transaction(F->host().c_str(), F->port(), 2)) // todo fix for other paths than /
  {
#ifdef DEBUG
    Debug_printf("Directory opened, handle ID: %d\n", tnfsPacket.data[1]);
#endif
    return tnfsPacket.data[1];
  }
  return -1;
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
bool tnfs_readdir(TNFSImpl *F, int fid, char *nextFile)
{
  tnfsSessionID_t sessionID = F->sid();

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;

  tnfsPacket.command = 0x11;      // READDIR
  tnfsPacket.data[0] = (byte)fid; // dir handle

#ifdef DEBUG_VERBOSE
  Debug_printf("\nTNFS Read next dir entry, host #%s - fid %02x\n", F->host().c_str(), fid);
#endif

  if (tnfs_transaction(F->host().c_str(), F->port(), 1))
  {
    if (tnfsPacket.data[0] == 0x00)
    {
      strcpy(nextFile, (char *)&tnfsPacket.data[1]);
#ifdef DEBUG
      Debug_printf("Entry: %s\n", nextFile);
#endif
      return true;
    }
  }
  return false;
}

// TNFS telldir
bool tnfs_telldir(TNFSImpl *F, int fid, long *pos)
{
  tnfsSessionID_t sessionID = F->sid();

  unsigned char *p;

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;
  tnfsPacket.command = 0x14;      // TELLDIR
  tnfsPacket.data[0] = (byte)fid; // dir handle

  if (tnfs_transaction(F->host().c_str(), F->port(), 1))
  {
    if (tnfsPacket.data[0] == 0x00)
    {
      p = &tnfsPacket.data[1];
      pos = (long *)p;
      return true;
    }
  }
  return false;
}

// TNFS seekdir
bool tnfs_seekdir(TNFSImpl *F, int fid, long pos)
{
  tnfsSessionID_t sessionID = F->sid();
  unsigned char offsetVal[4];

  offsetVal[0] = (int)((pos & 0xFF000000) >> 24);
  offsetVal[1] = (int)((pos & 0x00FF0000) >> 16);
  offsetVal[2] = (int)((pos & 0x0000FF00) >> 8);
  offsetVal[3] = (int)((pos & 0X000000FF));

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;
  tnfsPacket.command = 0x13; // SEEKDIR

  tnfsPacket.data[0] = (byte)fid; // dir handle
  tnfsPacket.data[1] = offsetVal[0];
  tnfsPacket.data[2] = offsetVal[1];
  tnfsPacket.data[3] = offsetVal[2];
  tnfsPacket.data[4] = offsetVal[3];

#ifdef DEBUG_VERBOSE
  Debug_printf("\nTNFS Seekdir, host #%s - fid %02x, pos: %lu\n", F->host().c_str(), fid, pos);
#endif

  if (tnfs_transaction(F->host().c_str(), F->port(), 5))
  {
    if (tnfsPacket.data[0] == 0x00)
      return true;
  }

  return false;
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
bool tnfs_closedir(TNFSImpl *F, int fid)
{
  tnfsSessionID_t sessionID = F->sid();

  tnfsPacket.session_idl = sessionID.session_idl;
  tnfsPacket.session_idh = sessionID.session_idh;
  tnfsPacket.command = 0x12;      // CLOSEDIR
  tnfsPacket.data[0] = (byte)fid; // Open root dir

  if (tnfs_transaction(F->host().c_str(),F->port(),1))
  {
    if (tnfsPacket.data[0]==0x00)
      return true;
  }
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
  Write to fid 4, 256 bytes of data:

  0xBEEF 0x00 0x22 0x04 0x00 0x01 ...data...

  The server replies with the standard header, followed by the return
  code, and the number of bytes actually written. For example:

  0xBEEF 0x00 0x22 0x00 0x00 0x01 - Successful write of 256 bytes
  0xBEEF 0x00 0x22 0x06 - Failed write, error is "bad file descriptor"
*/

size_t tnfs_write(TNFSImpl *F, int fid, const uint8_t *buf, unsigned short len)
{
  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < TNFS_RETRIES)
  {
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.retryCount++;        // Increase sequence
    tnfsPacket.command = 0x22;      // READ
    tnfsPacket.data[0] = (byte)fid; // returned file descriptor
    tnfsPacket.data[1] = len & 0xFF;
    tnfsPacket.data[2] = len >> 8;

#ifdef DEBUG_VERBOSE
    Debug_print("Writing to File descriptor: ");
    Debug_println(fid);
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println(" ");
#endif /* DEBUG_S */

    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.write(buf, len);
    UDP.endPacket();

    while (dur < TNFS_TIMEOUT)
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
          return len;
        }
        else
        {
// Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return 0;
        }
      }
    }
#ifdef DEBUG
    Debug_println("tnfs_write Timeout after 5000ms.");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("tnfs_write Failed.\n");
#endif
  return -1;
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
  Read from fid 4, maximum 256 bytes:

  0xBEEF 0x00 0x21 0x04 0x00 0x01

  The server will reply with the standard header, followed by the single
  byte return code, the actual amount of bytes read as a 16 bit unsigned
  little endian value, then the data, for example, 256 bytes:

  0xBEEF 0x00 0x21 0x00 0x00 0x01 ...data...

  End-of-file reached:

  0xBEEF 0x00 0x21 0x21
*/

size_t tnfs_read(TNFSImpl *F, int fid, uint8_t *buf, unsigned short len)
{

  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < TNFS_RETRIES)
  {
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.retryCount++;         // Increase sequence
    tnfsPacket.command = 0x21;       // READ
    tnfsPacket.data[0] = (byte)fid;  // returned file descriptor
    tnfsPacket.data[1] = len & 0xFF; // len bytes
    tnfsPacket.data[2] = len >> 8;   //

#ifdef DEBUG_VERBOSE
    Debug_print("Reading from File descriptor: ");
    Debug_println(fid);
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println(" ");
#endif /* DEBUG_S */

    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.endPacket();
    start = millis();
    dur = millis() - start;
    while (dur < TNFS_TIMEOUT)
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
          uint8_t *s = &tnfsPacket.data[3];
          memcpy(buf, s, len);
          return len;
        }
        else
        {
// Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return 0;
        }
      }
    }
#ifdef DEBUG
    Debug_println("tnfs_read Timeout after 5000ms.");
    if (retries < TNFS_RETRIES)
      Debug_printf("Retrying...\n");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("tnfs_read Failed.\n");
#endif
  return -1;
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
bool tnfs_seek(TNFSImpl *F, int fid, long offset)
{
  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  byte offsetVal[4];
  unsigned char retries = 0;

  while (retries < TNFS_RETRIES)
  {
    offsetVal[0] = (int)((offset & 0xFF000000) >> 24);
    offsetVal[1] = (int)((offset & 0x00FF0000) >> 16);
    offsetVal[2] = (int)((offset & 0x0000FF00) >> 8);
    offsetVal[3] = (int)((offset & 0X000000FF));

    tnfsPacket.retryCount++;
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.command = 0x25; // LSEEK
    tnfsPacket.data[0] = (byte)fid;
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
    Debug_println(" ");
#endif /* DEBUG_S*/

    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, 6 + 4);
    UDP.endPacket();

    while (dur < TNFS_TIMEOUT)
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
    Debug_println("tnfs_seek Timeout after 5000ms.");
    if (retries < TNFS_RETRIES)
      Debug_printf("Retrying...\n");
#endif /* DEBUG_S */
    tnfsPacket.retryCount--;
    retries++;
  }
#ifdef DEBUG
  Debug_printf("tnfs_seek Failed.\n");
#endif
  return false;
}

/*
-----------------------------------------------
STAT - Get information on a file - Command 0x24
-----------------------------------------------
  Reads the file's information, such as size, datestamp etc. The TNFS
  stat contains less data than the POSIX stat - information that is unlikely
  to be of use to 8 bit systems are omitted.
  The request consists of the standard header, followed by the full path
  of the file to stat, terminated by a NULL. Example:

  0xBEEF 0x00 0x24 /foo/bar/baz.txt 0x00

  The server replies with the standard header, followed by the return code.
  On success, the file information follows this. Stat information is returned
  in this order. Not all values are used by all servers. At least file
  mode and size must be set to a valid value (many programs depend on these).

  File mode       - 2 bytes: file permissions - little endian byte order
  uid             - 2 bytes: Numeric UID of owner
  gid             - 2 bytes: Numeric GID of owner
  size            - 4 bytes: Unsigned 32 bit little endian size of file in bytes
  atime           - 4 bytes: Access time in seconds since the epoch, little end.
  mtime           - 4 bytes: Modification time in seconds since the epoch,
                            little endian
  ctime           - 4 bytes: Time of last status change, as above.
  uidstring       - 0 or more bytes: Null terminated user id string
  gidstring       - 0 or more bytes: Null terminated group id string

  Fields that don't apply to the server in question should be left as 0x00.
  The ´mtime' field and 'size' fields are unsigned 32 bit integers.
  The uidstring and gidstring are helper fields so the client doesn't have
  to then ask the server for the string representing the uid and gid.

  File mode flags will be most useful for code that is showing a directory
  listing, and for programs that need to find out what kind of file (regular
  file or directory, etc) a particular file may be. They follow the POSIX
  convention which is:

  Flags           Octal representation
  S_IFDIR         0040000         Directory


  Most of these won't be of much interest to an 8 bit client, but the
  read/write/execute permissions can be used for a client to determine whether
  to bother even trying to open a remote file, or to automatically execute
  certain types of files etc. (Further file metadata such as load and execution
  addresses are platform specific and should go into a header of the file
  in question). Note the "trivial" bit in TNFS means that the client is
  unlikely to do anything special with a FIFO, so writing to a file of that
  type is likely to have effects on the server, and not the client! It's also
  worth noting that the server is responsible for enforcing read and write
  permissions (although the permission bits can help the client work out
  whether it should bother to send a request).
*/
tnfsStat_t tnfs_stat(TNFSImpl *F, const char *filename)
{
  tnfsStat_t retStat;

  tnfsSessionID_t sessionID = F->sid();

  int start = millis();
  int dur = millis() - start;
  int c = 0;
  unsigned char retries = 0;

  while (retries < TNFS_RETRIES)
  {
    tnfsPacket.session_idl = sessionID.session_idl;
    tnfsPacket.session_idh = sessionID.session_idh;
    tnfsPacket.retryCount++;   // increase sequence #
    tnfsPacket.command = 0x24; // STAT

    for (int i = 0; i < strlen(filename); i++)
    {
      tnfsPacket.data[c++] = filename[i];
    }

    tnfsPacket.data[c++] = 0x00;

#ifdef DEBUG_VERBOSE
    Debug_printf("Status: %s\n", filename);
    Debug_print("Req Packet: ");
    for (int i = 0; i < c + 4; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println(" ");
#endif /* DEBUG_S */

    UDP.beginPacket(F->host().c_str(), F->port());
    UDP.write(tnfsPacket.rawData, c + 4);
    UDP.endPacket();

    while (dur < TNFS_TIMEOUT)
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
          retStat.isDir = (tnfsPacket.data[2] & 0x40);
          retStat.fsize = tnfsPacket.data[7];
          retStat.fsize += tnfsPacket.data[8] * 0x00000100;
          retStat.fsize += tnfsPacket.data[9] * 0x00010000;
          retStat.fsize += tnfsPacket.data[10] * 0x01000000;
          retStat.mtime = tnfsPacket.data[15];
          retStat.mtime += tnfsPacket.data[16] * 0x00000100;
          retStat.mtime += tnfsPacket.data[17] * 0x00010000;
          retStat.mtime += tnfsPacket.data[18] * 0x01000000;
#ifdef DEBUG
          Debug_print("Returned directory status: ");
          Debug_println(retStat.isDir ? "true" : "false");
          Debug_print("File size: ");
          Debug_println(retStat.fsize, DEC);
          Debug_print("Last write time: ");
          Debug_println(retStat.mtime, DEC);
#endif
          return retStat;
        }
        else
        {
// unsuccessful
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return retStat;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("tnfs_stat Timeout after 5000ms.");
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("tnfs_stat Status Failed\n");
#endif
  return retStat;
}
