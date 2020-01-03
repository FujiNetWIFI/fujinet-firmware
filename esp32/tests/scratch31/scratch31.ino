/**
   Test #30 - start over.
*/

#ifdef ESP32
#include <SPI.h>
#include <SPIFFS.h>
#endif

#include <FS.h>
#include <driver/uart.h>

#ifdef ESP8266
#define SIO_UART Serial
#define BUG_UART Serial1
#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12
#endif
#ifdef ESP32
#define SIO_UART Serial2
#define BUG_UART Serial
#define PIN_LED1         2
#define PIN_LED2         4
#define PIN_INT         26
#define PIN_PROC        22
#define PIN_MTR         33
#define PIN_CMD         21
#endif

/**
   A Single command frame, both in structured and unstructured
   form.
*/
union
{
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

File atr;

/**
 * Calculate offset into ATR file from desired sector
 */
unsigned long atr_sector_to_offset(unsigned short s)
{
  return (unsigned long)(((s*128)-128)+16);
}

/**
   calculate 8-bit checksum.
*/
byte sio_checksum(byte* chunk, int length)
{
  int chkSum = 0;
  for (int i = 0; i < length; i++) {
    chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
  }
  return (byte)chkSum;
}

/**
 * sio NAK
 */
void sio_nak()
{
  SIO_UART.write('N');
}

/**
 * sio ACK
 */
void sio_ack()
{
  SIO_UART.write('A');
}

/**
 * sio COMPLETE
 */
void sio_complete()
{
  SIO_UART.write('C');
  SIO_UART.flush();  
}

/**
 * sio ERROR
 */
void sio_error()
{
  SIO_UART.write('E');
}

/**
 * sio READ from PERIPHERAL to COMPUTER
 */
void sio_read(byte* b, unsigned short len)
{
  byte ck=sio_checksum(b,len);

  // delayMicroseconds(250);

  // For now, we assume command completes
  sio_complete();

  // Write data frame.
  SIO_UART.write(b,len);

  // Write checksum
  SIO_UART.write(ck);
}

/**
 * SIO process drive status
 */
void sio_process_drive_status()
{
  byte status[4]={0x10,0xFF,0xFE,0x00};
  sio_read(status,sizeof(status));
}

/**
 * sio_process_drive_read()
 */
void sio_process_drive_read()
{
  byte sector[128];

  atr.read(sector,128);
  sio_read(sector,128);
}

/**
 * Process drive
 */
void sio_process_drive()
{
  unsigned short sector=(cmdFrame.aux2<<8)+cmdFrame.aux1;
  unsigned long offset=atr_sector_to_offset(sector);
  
  if (sio_checksum(cmdFrame.cmdFrameData,4)!=cmdFrame.cksum)
  {
    sio_nak();
    return;
  }
  else if (sector>720)
  {
    sio_nak();
    return;
  }
  else
    sio_ack();
    
  // Seek to desired position
  atr.seek(offset,SeekSet);

  switch(cmdFrame.comnd)
  {
    case 'R':
      sio_process_drive_read();
      break;
    case 'S':
      sio_process_drive_status();
      break;
  }
}

/**
   Setup
*/
void setup()
{
  SIO_UART.begin(19200);
  SIO_UART.setTimeout(8);

  // Set up pins
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_INT, HIGH);
  pinMode(PIN_PROC, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_PROC, HIGH);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
#ifdef ESP8266
  pinMode(PIN_LED, INPUT);
  digitalWrite(PIN_LED, HIGH); // off
#elif defined(ESP32)
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif

  SPIFFS.begin();
  atr=SPIFFS.open("/autorun.atr","r+");
}

/**
   Loop
*/
void loop()
{
  int a;
  if (digitalRead(PIN_CMD) == LOW)
  {
    memset(cmdFrame.cmdFrameData,0,5); // clear cmd frame.
    // delayMicroseconds(250); // computer is waiting for us to notice.

    // read cmd frame
    SIO_UART.readBytes(cmdFrame.cmdFrameData, 5);

    while (digitalRead(PIN_CMD) == LOW)
      yield();

    if (cmdFrame.devic==0x31)
      sio_process_drive();
  }
  else
  {
    a = SIO_UART.available();
    if (a)
    {
      while (SIO_UART.available())
      {
        SIO_UART.read(); // dump it.
      }
    }
  }
}
