#ifndef SIO_H
#define SIO_H
#include <Arduino.h>

#include "tnfs.h"

// pin configurations
// esp8266
#ifdef ESP_8266
#define SIO_UART Serial
#define PIN_LED 2
#define PIN_INT 5
#define PIN_PROC 4
#define PIN_MTR 16
#define PIN_CMD 12
// esp32
#elif defined(ESP_32)
#define SIO_UART Serial2
#define PIN_LED 2
#define PIN_INT 26
#define PIN_PROC 22
#define PIN_MTR 33
#define PIN_CMD 21
#endif

#ifdef ESP_8266
#include <FS.h>
#define INPUT_PULLDOWN INPUT_PULLDOWN_16 // for motor pin
#elif defined(ESP_32)
#include <SPIFFS.h>
#endif

#define DELAY_T5 1500
#define READ_CMD_TIMEOUT 12
#define CMD_TIMEOUT 50
#define STATUS_SKIP 8

/**
   ISR for falling COMMAND
*/
void ICACHE_RAM_ATTR sio_isr_cmd();

class sioDevice
{
private:
   File *_file;
   int _devnum=0x31;

   enum
   {
      ID,
      COMMAND,
      AUX1,
      AUX2,
      CHECKSUM,
      ACK,
      NAK,
      PROCESS,
      WAIT
   } cmdState;

   union {
      struct
      {
         unsigned char devic;
         unsigned char comnd;
         unsigned char aux1;
         unsigned char aux2;
         unsigned char cksum;
      };
      byte cmdFrameData[5];
   } cmdFrame;

   unsigned long cmdTimer = 0;
   byte statusSkipCount = 0;

   byte sector[128];

   byte sio_checksum(byte *chunk, int length);
   void sio_get_id();
   void sio_get_command();
   void sio_get_aux1();
   void sio_get_aux2();
   void sio_read();
   void sio_write();
   void sio_format();
   void sio_status();
   void sio_process();
   void sio_ack();
   void sio_nak();
   void sio_get_checksum();
   void sio_incoming();

public:
   sioDevice();
   ~sioDevice() {};
   void setup(File *f, int devNum);
   void setup(File *f);
   void handle();
};

#endif // guard