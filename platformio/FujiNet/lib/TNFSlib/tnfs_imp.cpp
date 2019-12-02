#include "tnfs_imp.h"

extern tnfsPacket_t tnfsPacket;

TNFSImpl::TNFSImpl() { }

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

