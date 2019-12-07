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

void str2packet(const char *s);
bool tnfs_mount(const char *host, uint16_t port=16384, const char *location="/", const char *userid="", const char *password="");
int tnfs_open(const char *host, uint16_t port, const char *filename, byte flag_lsb=1, byte flag_msb=0);
int tnfs_read(const char *host, uint16_t port, byte fd, size_t size);
void tnfs_seek(const char *host, uint16_t port, byte fd, uint32_t offset);

#endif // _TNFS_UDP_H

