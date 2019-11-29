#ifndef TNFS_H
#define TNFS_H

#include <Arduino.h>


#include <WiFiUdp.h>

#define TNFS_SERVER "mozzwald.com"
#define TNFS_PORT 16384

/*
SPIFFS File class definition

*/


class tnfsClient : public Stream
{
private:  

byte tnfs_fd;

union
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
} tnfsPacket;


/**
 * Mount the TNFS server
 */
void tnfs_mount();

/**
 * Open 'autorun.atr'
 */
void tnfs_open();

void tnfs_read();
 

public:

  void begin();
  
  size_t write(uint8_t);

  int available();
  int peek();
  void flush();

/**
 * TNFS read
 */
int read();
void read(byte arr[], int count);

/**
 * TNFS seek
 */
void seek(long offset);

};

#endif
