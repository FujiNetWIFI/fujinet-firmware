#ifndef _TNFS_UDP_H
#define _TNFS_UDP_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <tnfs.h>



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

bool tnfs_mount(unsigned char hostSlot);
bool tnfs_open(unsigned char deviceSlot, unsigned char options, bool create);
bool tnfs_close(unsigned char deviceSlot);
bool tnfs_opendir(unsigned char hostSlot);
bool tnfs_readdir(unsigned char hostSlot);
bool tnfs_closedir(unsigned char hostSlot);
bool tnfs_write(unsigned char deviceSlot, unsigned short len);
bool tnfs_read(unsigned char deviceSlot, unsigned short len);
bool tnfs_seek(unsigned char deviceSlot, long offset);
// bool tnfs_write_blank_atr(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors);

#endif // _TNFS_UDP_H

