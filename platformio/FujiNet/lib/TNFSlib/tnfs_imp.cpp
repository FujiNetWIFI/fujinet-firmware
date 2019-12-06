#include "tnfs_imp.h"

extern tnfsPacket_t tnfsPacket;

TNFSImpl::TNFSImpl() { }


FileImplPtr TNFSImpl::open(const char *path, const char *mode)
{
/* TODO: Map tnfs flags to "r", "w", "a", "r+", "w+", "a+"
Flags are a bit field. The flags are:
O_RDONLY        0x0001  Open read only
O_WRONLY        0x0002  Open write only
O_RDWR          0x0003  Open read/write
O_APPEND        0x0008  Append to the file, if it exists (write only)
O_CREAT         0x0100  Create the file if it doesn't exist (write only)
O_TRUNC         0x0200  Truncate the file on open for writing
O_EXCL          0x0400  With O_CREAT, returns an error if the file exists
*/
  byte flag_lsb = 1;
  byte flag_msb = 0;  
  tnfs_open(path,flag_lsb,flag_msb); 
  // TODO need to store the File Descriptor in to the object that is created below.
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

