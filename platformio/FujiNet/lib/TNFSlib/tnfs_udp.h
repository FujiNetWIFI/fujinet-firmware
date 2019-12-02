#ifndef _TNFS_UDP_H
#define _TNFS_UDP_H

#include <Arduino.h>
#include <WiFiUdp.h>

#define TNFS_SERVER "mozzwald.com"
#define TNFS_PORT 16384

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

#endif // _TNFS_UDP_H

