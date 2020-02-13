#ifndef _TNFS_IMP_H
#define _TNFS_IMP_H
#include <Arduino.h>
#include "debug.h"
#include <string.h>
#include <WiFiUdp.h>

#include "tnfs.h"
#include <FS.h>
#include <FSImpl.h>
//#include "tnfs_udp.h"

// https://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html
#define TNFS_RDONLY 0x0001 //Open read only
#define TNFS_WRONLY 0x0002 //Open write only
#define TNFS_RDWR 0x0003   //Open read/write
#define TNFS_APPEND 0x0008 //Append to the file, if it exists (write only)
#define TNFS_CREAT 0x0100  //Create the file if it doesn't exist (write only)
#define TNFS_TRUNC 0x0200  //Truncate the file on open for writing
#define TNFS_EXCL 0x0400   //With TNFS_CREAT, returns an error if the file exists

extern WiFiUDP UDP;

using namespace fs;

union tnfsPacket_t {
  struct
  {
    byte session_idl;
    byte session_idh;
    byte retryCount;
    byte command;
    byte data[508];
  };
  byte rawData[512];
};

struct tnfsSessionID_t
{
  unsigned char session_idl;
  unsigned char session_idh;
};

struct tnfsStat_t
{
  boolean isDir;
  size_t fsize;
  time_t mtime;
};

class TNFSFileImpl;

class TNFSImpl : public FSImpl
{
  //This class implements the physical interface for built-in functions in FS.h
protected:
  // TNFS host parameters
  std::string _host = "";
  uint16_t _port;
  tnfsSessionID_t _sid;
  std::string _location = "";
  std::string _userid = "";
  std::string _password = "";

public:
  FileImplPtr open(const char *path, const char *mode) override;
  bool exists(const char *path) override;
  bool rename(const char *pathFrom, const char *pathTo) override;
  bool remove(const char *path) override;
  bool mkdir(const char *path) override;
  bool rmdir(const char *path) override;
  std::string host();
  uint16_t port();
  tnfsSessionID_t sid();
  std::string location();
  std::string userid();
  std::string password();
};

class TNFSFileImpl : public FileImpl
{
  //This class implements the physical interface for built-in functions in the File class defined in FS.h

protected:
  TNFSImpl *fs;
  byte fid;
  char fn[256];
  tnfsStat_t stats;

public:
  TNFSFileImpl(TNFSImpl *fs, byte fid, const char *filename, tnfsStat_t stats);
  ~TNFSFileImpl();
  size_t write(const uint8_t *buf, size_t size) override;
  size_t read(uint8_t *buf, size_t size) override;
  void flush() override;
  bool seek(uint32_t pos, SeekMode mode) override;
  size_t position() const override;
  size_t size() const override;
  void close() override;
  const char *name() const override;
  time_t getLastWrite() override;
  boolean isDirectory(void) override;
  FileImplPtr openNextFile(const char *mode) override;
  void rewindDirectory(void) override;
  operator bool();
};

tnfsSessionID_t tnfs_mount(FSImplPtr hostPtr);
int tnfs_open(TNFSImpl *F, const char *mountPath, byte flag_lsb, byte flag_msb);
bool tnfs_close(TNFSImpl *F, byte fid, const char *mountPath);
int tnfs_opendir(TNFSImpl *F, const char *dirName);
bool tnfs_readdir(TNFSImpl *F, byte fid, char *nextFile);
bool tnfs_closedir(TNFSImpl *F, byte fid);
size_t tnfs_write(TNFSImpl *F, byte fid, const uint8_t *buf, unsigned short len);
size_t tnfs_read(TNFSImpl *F, byte fid, uint8_t *buf, unsigned short size);
bool tnfs_seek(TNFSImpl *F, byte fid, long offset);
tnfsStat_t tnfs_stat(TNFSImpl *F, const char *filename);

//todo:  bool tnfs_write_blank_atr(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors);

#endif //_TNFS_IMP_H
