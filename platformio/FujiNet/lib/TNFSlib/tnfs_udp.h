#ifndef _TNFS_UDP_H
#define _TNFS_UDP_H

#include <Arduino.h>
#include <WiFiUdp.h>
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

tnfsSessionID_t tnfs_mount(FSImplPtr hostPtr);//(unsigned char hostSlot);
//bool tnfs_open(unsigned char deviceSlot, unsigned char options, bool create);
bool tnfs_open(TNFSImpl* F, const char *mountPath, byte flag_lsb, byte flag_msb);
bool tnfs_close(unsigned char deviceSlot);
bool tnfs_opendir(unsigned char hostSlot);
bool tnfs_readdir(unsigned char hostSlot);
bool tnfs_closedir(unsigned char hostSlot);
bool tnfs_write(unsigned char deviceSlot, unsigned short len);
bool tnfs_read(unsigned char deviceSlot, unsigned short len);
bool tnfs_seek(unsigned char deviceSlot, long offset);
// bool tnfs_write_blank_atr(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors);

#endif // _TNFS_UDP_H

