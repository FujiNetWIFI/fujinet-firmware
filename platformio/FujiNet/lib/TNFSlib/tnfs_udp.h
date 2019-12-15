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

void str2packet(String S);
//void hostname(const char* mp);
//uint16_t portnum(const char* mp);
bool tnfs_mount(String host, int port=16384, String location="/", String userid="", String password="");
int tnfs_open(String host, int port, String filename, byte flag_lsb=1, byte flag_msb=0);
int tnfs_read(String host, int port, byte fd, size_t size);
void tnfs_write(String host, int port, byte fd, const uint8_t *sector, size_t size);
void tnfs_seek(String host, int port, byte fd, uint32_t offset);

#endif // _TNFS_UDP_H

