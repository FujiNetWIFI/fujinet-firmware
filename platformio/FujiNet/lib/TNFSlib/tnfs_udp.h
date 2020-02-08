#ifndef _TNFS_UDP_H
#define _TNFS_UDP_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "debug.h"

#include "tnfs.h"
#include "tnfs_imp.h"


union tnfsPacket_t
{
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

tnfsSessionID_t tnfs_mount(FSImplPtr hostPtr);
int tnfs_open(TNFSImpl* F, const char *mountPath, byte flag_lsb, byte flag_msb);
bool tnfs_close(TNFSImpl* F, byte fd, const char *mountPath);
int tnfs_opendir(TNFSImpl* F, const char* dirName);
bool tnfs_readdir(TNFSImpl* F,byte fd, char* nextFile);
bool tnfs_closedir(TNFSImpl* F, byte fd);
size_t tnfs_write(TNFSImpl* F, byte fd, const uint8_t* buf, unsigned short len);
size_t tnfs_read(TNFSImpl* F, byte fd, uint8_t* buf, unsigned short size);
bool tnfs_seek(TNFSImpl* F, byte fd, long offset);
bool tnfs_stat(TNFSImpl *F, const char *filename);
// bool tnfs_write_blank_atr(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors);

#endif // _TNFS_UDP_H

