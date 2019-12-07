#include "tnfs_imp.h"

extern tnfsPacket_t tnfsPacket;

/* File Ssstem Implementation */

TNFSImpl::TNFSImpl() {}

FileImplPtr TNFSImpl::open(const char *path, const char *mode)
{
  byte fd;

  // extract host and port from mountpoint
  char host[255];
  int port=0;
  //M(mountpoint());
  sscanf(mountpoint(),"//%s:%d/",host,&port);
  //int n = M.lastIndexOf(":");
  //String host = M.substring(2,n);
  //uint16_t port = M.substring(n+1).toInt();

  // TODO: path (filename) checking

  uint16_t flag = TNFS_RDONLY; // https://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html
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
        flag = TNFS_RDWR;
        break;
      case 'w':
        flag = TNFS_RDWR | TNFS_CREAT | TNFS_TRUNC;
        break;
      case 'a':
        flag = TNFS_RDWR | TNFS_CREAT | TNFS_APPEND;
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
  flag_msb = byte(flag >> 8);

  int temp = tnfs_open(String((const char*)host), port, path, flag_lsb, flag_msb);
  if (temp >= 0)
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

TNFSFileImpl::TNFSFileImpl(TNFSImpl *fs, byte fd) : _fs(fs), _fd(fd) 
{
  char host[255];
  int port=0;
  sscanf(_fs->mountpoint(),"//%s:%d/",host,&port);
  _host = String(host);
  _port = (uint16_t)port;
}

size_t TNFSFileImpl::write(const uint8_t *buf, size_t size)
{
  return size;
}

size_t TNFSFileImpl::read(uint8_t *buf, size_t size)
{
  BUG_UART.println("calling tnfs_read");
  //BUG_UART.println(_fs->mountpoint());
  int ret = tnfs_read(_host, _port, _fd, size);
  if (size == ret)
  {
    for (int i = 0; i < size; i++)
      buf[i] = tnfsPacket.data[i + 3];
    return size;
  }
  return 0;
}

void TNFSFileImpl::flush() {}

bool TNFSFileImpl::seek(uint32_t pos, SeekMode mode)
{
  tnfs_seek(_host, _port, _fd, pos); // implement SeekMode
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
