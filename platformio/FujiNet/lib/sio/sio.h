#ifndef SIO_H
#define SIO_H

#include <forward_list>

#include "fnSystem.h"

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
//#define SIO_UART Serial2
#define PIN_INT 26
#define PIN_PROC 22

#ifdef BOARD_HAS_PSRAM
#define PIN_MTR 36
#define PIN_CMD 39
#else
#define PIN_MTR 33
#define PIN_CMD 21
#endif

#define PIN_CKO 32
#define PIN_CKI 27
#endif

#define DELAY_T4 850
#define DELAY_T5 250
#define READ_CMD_TIMEOUT 12
#define CMD_TIMEOUT 50
#define STATUS_SKIP 8

//#define ADDR_R 0x50
//#define ADDR_P 0x40

#define SIO_DAISYCHAIN_STARTING_SIZE 32

#define SIO_DEVICEID_FUJINET 0x70
#define SIO_DEVICEID_FN_NETWORK 0x71
#define SIO_DEVICEID_FN_NETWORK_LAST 0x78
#define SIO_DEVICEID_FN_VOICE 0x43

#define SIO_DEVICEID_APETIME 0x45
#define SIO_DEVICEID_RS232 0x50
#define SIO_DEVICEID_DISK 0x31
#define SIO_DEVICEID_PRINTER 0x40
#define SIO_DEVICEID_TYPE3POLL 0x4F

// Not used, but for reference:
#define SIO_DEVICEID_SIO2BT_NET 0x4E
#define SIO_DEVICEID_SIO2BT_SMART 0x45 // Doubles as APETime and "High Score Submission" to URL

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
class sioModem;   // declare here so can reference it, but define in modem.h
class sioFuji;    // declare here so can reference it, but define in fuji.h
class sioBus;     // declare early so can be friend
class sioNetwork; // declare here so can reference it, but define in network.h

class sioDevice
{
protected:
   friend sioBus;

   int _devnum;

   cmdFrame_t cmdFrame;
   bool listen_to_type3_polls = false;
   unsigned char status_wait_count = 5;

   void sio_to_computer(byte *b, unsigned short len, bool err);
   byte sio_to_peripheral(byte *b, unsigned short len);

   void sio_ack();
   void sio_nak();
   //void sio_get_checksum();
   void sio_complete();
   void sio_error();
   unsigned short sio_get_aux();
   virtual void sio_status() = 0;
   virtual void sio_process() = 0;

public:
   int id() { return _devnum; };
   bool is_config_device = false;
   bool device_active = true;
};

class sioBus
{
private:
   std::forward_list<sioDevice *> daisyChain;
   unsigned long cmdTimer = 0;
   sioDevice *activeDev = nullptr;
   sioModem *modemDev = nullptr;
   sioFuji *fujiDev = nullptr;
   sioNetwork *netDev[8] = {nullptr};
   int sioBaud = 19200; // SIO Baud rate

public:
   void setup();
   void service();
   int numDevices();
   void addDevice(sioDevice *pDevice, int device_id);
   void remDevice(sioDevice *pDevice);
   sioDevice *deviceById(int device_id);
   int getBaudrate();
   void setBaudrate(int baudrate);
};

extern sioBus SIO;

#endif // guard
