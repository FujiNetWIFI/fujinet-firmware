#include "tnfs_imp.h"

extern tnfsPacket_t tnfsPacket;

/* File System Implementation */

//TNFSImpl::TNFSImpl() {}

std::string TNFSImpl::host()
{

  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%s", temp);
    if (n == 1)
      _host = temp;
    else
      _host.clear();
  }
  return _host;
}

uint16_t TNFSImpl::port()
{
  if (_mountpoint != NULL)
  {
    uint16_t temp;
    int n = sscanf(_mountpoint, "%*s %u", &temp);
    if (n == 1)
      _port = temp;
  }
  return _port;
}

tnfsSessionID_t TNFSImpl::sid()
{
  if (_mountpoint != NULL)
  {
    byte lo;
    byte hi;
    int n = sscanf(_mountpoint, "%*s %*u &u &u", &lo, &hi);
    if (n == 1)
      _sid.session_idl = lo;
    _sid.session_idh = hi;
  }
  return _sid;
}

std::string TNFSImpl::location()
{
  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%*s %*u %s", temp);
    if (n == 1)
      _location = temp;
    else
      _location.clear();
  }
  return _location;
}

std::string TNFSImpl::userid()
{
  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%*s %*u %*s %s", temp);
    if (n == 1)
      _userid = temp;
    else
      _userid.clear();
  }
  return _userid;
}

std::string TNFSImpl::password()
{
  if (_mountpoint != NULL)
  {
    char temp[36];
    int n = sscanf(_mountpoint, "%*s %*u %*s %*s %s", temp);
    if (n == 1)
      _password = temp;
    else
      _password.clear();
  }
  return _password;
}

FileImplPtr TNFSImpl::open(const char *path, const char *mode)
{
  byte fd;

  // TODO: path (filename) checking

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

  int temp = tnfs_open(this, path, flag_lsb, flag_msb);
  if (temp != -1)
  {
    fd = (byte)temp;
  }
  else
  {
    return nullptr;
  }
  return std::make_shared<TNFSFileImpl>(this, fd, path);
}

bool TNFSImpl::exists(const char *path)
{
  File f = open(path, "r");
  return (f == true && !f.isDirectory());
}

bool TNFSImpl::rename(const char *pathFrom, const char *pathTo) { return false; }
bool TNFSImpl::remove(const char *path) { return false; }
bool TNFSImpl::mkdir(const char *path) { return false; }
bool TNFSImpl::rmdir(const char *path) { return false; }

/* File Implementation */

TNFSFileImpl::TNFSFileImpl(TNFSImpl *fs, byte fd, const char *name)
{
  this->fs = fs;
  this->fd = fd;
  strcpy(this->name, name);
}

size_t TNFSFileImpl::write(const uint8_t *buf, size_t size)
{
#ifdef DEBUG_S
  BUG_UART.println("calling tnfs_write");
#endif
  size_t ret = tnfs_write(fs, fd, buf, size);
  if (size == ret)
    return size;
  else
    return 0;
}

size_t TNFSFileImpl::read(uint8_t *buf, size_t size)
{
#ifdef DEBUG_S
  BUG_UART.println("calling tnfs_read");
#endif
  size_t ret = tnfs_read(fs, fd, buf, size);
  // move this part into tnfs_read and pass a buffer instead
  if (size == ret)
    return size;
  else
    return 0;
}

void TNFSFileImpl::flush() {}

bool TNFSFileImpl::seek(uint32_t pos, SeekMode mode)
{
  tnfs_seek(fs, fd, pos); // implement SeekMode
  return true;
}

void TNFSFileImpl::close()
{
  tnfs_close(fs, fd, this->name);
}

const char *TNFSFileImpl::name() const
{
  return this->name;
}

// not written yet
size_t TNFSFileImpl::position() const { return 0; }
size_t TNFSFileImpl::size() const { return 0; }
time_t TNFSFileImpl::getLastWrite() { return 0; }
boolean TNFSFileImpl::isDirectory(void) { return false; }
FileImplPtr TNFSFileImpl::openNextFile(const char *mode) { return FileImplPtr(); }
void TNFSFileImpl::rewindDirectory(void) {}
TNFSFileImpl::operator bool() { return true; }
