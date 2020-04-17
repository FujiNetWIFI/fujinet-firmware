#ifndef SIO_H
#define SIO_H
#include <Arduino.h>
#include "debug.h"

//#include "../LinkedList-1.2.3/LinkedList.h"
#include "LinkedList.h"

// pin configurations
#ifdef ESP8266
#define SIO_UART Serial
#define PIN_INT 5
#define PIN_PROC 4
#define PIN_MTR 16
#define INPUT_PULLDOWN INPUT_PULLDOWN_16
#define PIN_CMD 12
#define PIN_CKI 14
//#define PIN_CKO         2
#define DELAY_T0 750
#define DELAY_T1 650
#define DELAY_T2 0
#define DELAY_T3 1000
#elif defined(ESP32)
#define SIO_UART Serial2
#define PIN_INT 26
#define PIN_PROC 22
#define PIN_MTR 33
#define PIN_CMD 21
#define PIN_CKO 32
#define PIN_CKI 27
#define PIN_SIO5V 35
#endif

#define DELAY_T4 850
#define DELAY_T5 250
#define READ_CMD_TIMEOUT 12
#define CMD_TIMEOUT 50
#define STATUS_SKIP 8

#define ADDR_R 0x50
#define ADDR_P 0x40

union cmdFrame_t {
   struct
   {
      unsigned char devic;
      unsigned char comnd;
      unsigned char aux1;
      unsigned char aux2;
      unsigned char cksum;
   };
   byte cmdFrameData[5];
};

//helper functions
byte sio_checksum(byte *chunk, int length);
void sio_flush();

// class def'ns
class sioModem; // declare here so can reference it, but define in modem.h
class sioFuji;  // declare here so can reference it, but define in fuji.h
class sioBus;   // declare early so can be friend

class sioDevice
{
protected:
   friend sioBus;

   int _devnum;
   //String _devname; // causes linker error " undefined reference to `vtable for sioDevice' "

   cmdFrame_t cmdFrame;

   void sio_to_computer(byte *b, unsigned short len, bool err);
   byte sio_to_peripheral(byte *b, unsigned short len);

   void sio_ack();
   void sio_nak();
   //void sio_get_checksum();
   void sio_complete();
   void sio_error();
   unsigned short sio_get_aux();
   virtual void sio_status()=0;
   virtual void sio_process()=0;

public:
   int id() { return _devnum; };
};

class sioBus
{
private:
   LinkedList<sioDevice *> daisyChain = LinkedList<sioDevice *>();
   unsigned long cmdTimer = 0;
   sioDevice *activeDev = nullptr;
   sioModem *modemDev = nullptr;
   sioFuji *fujiDev = nullptr;

   int sioBaud = 19200; // SIO Baud rate

public:
   int numDevices();

   void setup();
   void service();
   void addDevice(sioDevice *p, int N);
   bool remDevice(sioDevice *p);
   sioDevice *device(int i);
   int getBaudrate();
   void setBaudrate(int baudrate);
#ifdef ESP32
   int sio_volts();
#endif
};

extern sioBus SIO;

#endif // guard
