#include "tnfs_imp.h"

extern tnfsPacket_t tnfsPacket;

/* File Ssstem Implementation */

TNFSImpl::TNFSImpl() {}

FileImplPtr TNFSImpl::open(const char *path, const char *mode)
{
  byte fd;
  // TODO: path (filename) checking
/* TODO: Map tnfs flags to "r", "w", "a", "r+", "w+", "a+"
"r"	read: Open file for input operations. The file must exist.
"w"	write: Create an empty file for output operations. If a file with the same name already exists, its contents are discarded.
"a"	append: Open file for output at the end of a file. Output operations always write data at the end of the file, expanding it. 
"r+"	read/update: Open a file for update (both for input and output). The file must exist.
"w+"	write/update: Create an empty file and open it for update (both for input and output). 
"a+"	append/update: Open a file for update (both for input and output) with all output operations writing data at the end of the file. 
Flags are a bit field. The flags are:
*/
#define O_RDONLY 0x0001 //Open read only
#define O_WRONLY 0x0002 //Open write only
#define O_RDWR 0x0003   //Open read/write
#define O_APPEND 0x0008 //Append to the file, if it exists (write only)
#define O_CREAT 0x0100  //Create the file if it doesn't exist (write only)
#define O_TRUNC 0x0200  //Truncate the file on open for writing
#define O_EXCL 0x0400   //With O_CREAT, returns an error if the file exists
/*
https://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html
mode      open() Flags
r         O_RDONLY
w         O_WRONLY|O_CREAT|O_TRUNC
a         O_WRONLY|O_CREAT|O_APPEND
r+        O_RDWR
w+        O_RDWR|O_CREAT|O_TRUNC
a+        O_RDWR|O_CREAT|O_APPEND
*/
  uint16_t flag = O_RDONLY;
  byte flag_lsb;
  byte flag_msb;
  if (strlen(mode) == 1)
  {
    switch (mode[0])
    {
    case 'r':
      flag = O_RDONLY;
      break;
    case 'w':
      flag = O_WRONLY | O_CREAT | O_TRUNC;
      break;
    case 'a':
      flag = O_WRONLY | O_CREAT | O_APPEND;
      break;
    default:
      return NULL;
    }
  }
  else if (strlen(mode) == 2)
  {
    if (mode[1] == '+')
    {
      switch (mode[0])
      {
      case 'r':
        flag = O_RDWR;
        break;
      case 'w':
        flag = O_RDWR | O_CREAT | O_TRUNC;
        break;
      case 'a':
        flag = O_RDWR | O_CREAT | O_APPEND;
        break;
      default:
        return NULL;
      }
    }
    else
    {
      return NULL;
    }
  }
  flag_lsb = byte(flag & 0xff);
  flag_msb = byte(flag>>8);
  int temp = tnfs_open(path, flag_lsb, flag_msb);
  if (temp > 0)
  {
    fd = (byte)temp;
  }
  else
  {
    fd = 0;
    // send debug message with -temp as error
    return NULL;
  }
  return std::make_shared<TNFSFileImpl>(this, fd);
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

/* File Implementation */

TNFSFileImpl::TNFSFileImpl(TNFSImpl *fs, byte fd) : _fs(fs), _fd(fd) {}

size_t TNFSFileImpl::write(const uint8_t *buf, size_t size) { return size; }
size_t TNFSFileImpl::read(uint8_t *buf, size_t size)
{
  tnfs_read();
  for (int i = 0; i < size; i++)
    buf[i] = tnfsPacket.data[i + 3];
  return size;
}
void TNFSFileImpl::flush() {}
bool TNFSFileImpl::seek(uint32_t pos, SeekMode mode)
{
  tnfs_seek(pos);
  return true;
}
size_t TNFSFileImpl::position() const { return 0; }
size_t TNFSFileImpl::size() const { return 0; }
void TNFSFileImpl::close() {}
const char *TNFSFileImpl::name() const { return 0; }
time_t TNFSFileImpl::getLastWrite() { return 0; }
boolean TNFSFileImpl::isDirectory(void) { return false; }
FileImplPtr TNFSFileImpl::openNextFile(const char *mode) { return FileImplPtr(); }
void TNFSFileImpl::rewindDirectory(void) {}
TNFSFileImpl::operator bool() { return true; }

/* Thom's things */
