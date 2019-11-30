#ifndef _TNFS_THINGS_H
#define _TNFS_THINGS_H

#include <Arduino.h>
#include <WiFiUdp.h>

#define TNFS_SERVER "mozzwald.com"
#define TNFS_PORT 16384

void tnfs_mount(const char *host, uint16_t port);
void tnfs_open();
void tnfs_read();
void tnfs_seek(uint32_t offset);

#endif //_TNFS_THINGS_H
