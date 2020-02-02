#ifndef SIO_H
#define SIO_H
#include <Arduino.h>
#include "debug.h"

#include "LinkedList.h"

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
#define PIN_INT 26
#define PIN_PROC 22
#define PIN_MTR 33
#define PIN_CMD 21
#define PIN_LED1 2
#define PIN_LED2 4
#endif

#define DELAY_T5 1500
#define READ_CMD_TIMEOUT 12
#define CMD_TIMEOUT 50
#define STATUS_SKIP 8

// enum cmdState_t
// {
//    ID,
//    COMMAND,
//    //AUX1,
//    //AUX2,
//    //CHECKSUM,
//    ACK,
//    NAK,
//    //PROCESS,
//    WAIT
// };

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

/**
   ISR for falling COMMAND
*/
// void ICACHE_RAM_ATTR sio_isr_cmd();

//helper functions
byte sio_checksum(byte *chunk, int length);

// class def'ns
class sioBus;
class sioDevice
{
protected:
   friend sioBus;

   int _devnum;
   //String _devname; // causes linker error " undefined reference to `vtable for sioDevice' "

   // cmdState_t cmdState; // PROCESS state not used

   cmdFrame_t cmdFrame;

   // unsigned long cmdTimer = 0;

   //byte sio_checksum(byte *chunk, int length); // moved outside the class def'n
   //void sio_get_id();
   //void sio_get_command();
   //void sio_get_aux1();
   //void sio_get_aux2();

   void sio_to_computer(byte* b, unsigned short len, bool err);
   byte sio_to_peripheral(byte* b, unsigned short len);

   void sio_ack();
   void sio_nak();
   //void sio_get_checksum();
   void sio_complete();
   void sio_error();
   virtual void sio_status();
   virtual void sio_process();
   //void sio_incoming();

public:
   //sioDevice() : cmdState(WAIT){};
   //sioDevice(int devnum) : _devnum(devnum), cmdState(WAIT){};
   //void service();
   int id() { return _devnum; };
   //String name() { return _devname; };
};

class sioBus
{
private:
   LinkedList<sioDevice *> daisyChain = LinkedList<sioDevice *>();
   unsigned long cmdTimer = 0;
   // enum
   // {
   //    BUS_ID,
   //    BUS_ACTIVE,
   //    BUS_WAIT
   // } busState = BUS_WAIT;
   sioDevice *activeDev = nullptr;

   // void sio_get_id();
   void sio_led(bool onOff);

public:
   void setup();
   void service();
   void addDevice(sioDevice *p, int N); //, String name);
   void remDevice(sioDevice *p);
   sioDevice *device(int i);
   int numDevices();
   long sioBaud = 19200; // SIO Baud rate
};

extern sioBus SIO;

#endif // guard
