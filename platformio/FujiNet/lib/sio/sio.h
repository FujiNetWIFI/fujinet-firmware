#ifndef SIO_H
#define SIO_H
#include <Arduino.h>

// pin configurations
// esp8266
#ifdef ESP_8266
#define SIO_UART Serial
#define PIN_LED 2
#define PIN_INT 5
#define PIN_PROC 4
#define PIN_MTR 16
#define INPUT_PULLDOWN INPUT_PULLDOWN_16
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
protected:
   int _devnum = 0x31;

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
   } cmdState; // PROCESS state not used

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

   byte sio_checksum(byte *chunk, int length);
   void sio_get_id();
   void sio_get_command();
   void sio_get_aux1();
   void sio_get_aux2();
   void sio_ack();
   void sio_nak();
   void sio_get_checksum();
   virtual void sio_status();
   virtual void sio_process();
   void sio_incoming();

public:
   sioDevice() : cmdState(WAIT){};
   sioDevice(int devnum) : _devnum(devnum), cmdState(WAIT){};
   void service();
   int id() { return _devnum; };
};

class sioBus
{
private:
   struct connection
   {
      int id;
      sioDevice *devPtr;
      connection *next;
   };
   connection *head = nullptr;

public:
   void setup();
   void addDevice(sioDevice *p);
};

extern sioBus SIO;

#endif // guard