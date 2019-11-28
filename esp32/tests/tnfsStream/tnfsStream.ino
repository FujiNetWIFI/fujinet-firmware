/**
 * Test #7 - Combine TNFS and SIO to boot Jumpman
 * Yes, this is a proof of concept, watch out for falling rocks.
 */

#include <WiFi.h>
#include <WiFiUdp.h>

#define TNFS_SERVER "192.168.1.114"
#define TNFS_PORT 16384

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug on 2nd UART (GPIO 2)
#define DEBUG_S

#define SIO_UART Serial2
#define BUG_UART Serial

#define PIN_LED         2
#define PIN_INT         26
#define PIN_PROC        22
#define PIN_MTR         33
#define PIN_CMD         21

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

unsigned long cmdTimer = 0;
byte statusSkipCount = 0;

WiFiUDP UDP;
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

class tnfsClient : public Stream
{

  
/**
 * Mount the TNFS server
 */
void tnfs_mount()
{
  int start=millis();
  int dur=millis()-start;
  
  memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));
  tnfsPacket.session_idl=0;
  tnfsPacket.session_idh=0;
  tnfsPacket.retryCount=0;
  tnfsPacket.command=0;
  tnfsPacket.data[0]=0x01;   // vers
  tnfsPacket.data[1]=0x00;   // "  "
  tnfsPacket.data[2]=0x2F;   // /
  tnfsPacket.data[3]=0x00;   // nul 
  tnfsPacket.data[4]=0x00;   // no username
  tnfsPacket.data[5]=0x00;   // no password

#ifdef DEBUG_S
  BUG_UART.print("Mounting / from ");
  BUG_UART.println(TNFS_SERVER);
  BUG_UART.print("Req Packet: ");
  for (int i=0;i<10;i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,10);
  UDP.endPacket();

  while (dur < 5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,512);
#ifdef DEBUG_S
      BUG_UART.print("Resp Packet: ");
      for (int i=0;i<l;i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        SIO_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
#ifdef DEBUG_S
        BUG_UART.print("Successful, Session ID: ");
        BUG_UART.print(tnfsPacket.session_idl, HEX);
        BUG_UART.println(tnfsPacket.session_idh, HEX);
#endif /* DEBUG_S */
        return;  
      }
      else
      {
        // Error
#ifdef DEBUG_S
        BUG_UART.print("Error #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
        return;  
      }
    }
  }
  // Otherwise we timed out.
#ifdef DEBUG_S
BUG_UART.println("Timeout after 5000ms");
#endif /* DEBUG_S */
}

/**
 * Open 'autorun.atr'
 */
void tnfs_open()
{
  int start=millis();
  int dur=millis()-start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command=0x29;  // OPEN
  tnfsPacket.data[0]=0x01;  // R/O
  tnfsPacket.data[1]=0x00;  //
  tnfsPacket.data[2]=0x00;  // Flags
  tnfsPacket.data[3]=0x00;  //
  tnfsPacket.data[4]='/';   // Filename start
  tnfsPacket.data[5]='a';
  tnfsPacket.data[6]='u';
  tnfsPacket.data[7]='t';
  tnfsPacket.data[8]='o';
  tnfsPacket.data[9]='r';
  tnfsPacket.data[10]='u';
  tnfsPacket.data[11]='n';
  tnfsPacket.data[12]='.';
  tnfsPacket.data[13]='a';
  tnfsPacket.data[14]='t';
  tnfsPacket.data[15]='r';
  tnfsPacket.data[16]=0x00; // NUL terminated
  tnfsPacket.data[17]=0x00; // no username
  tnfsPacket.data[18]=0x00; // no password

#ifdef DEBUG_S
  BUG_UART.println("Opening /autorun.atr...");
  BUG_UART.print("Req packet: ");
  for (int i=0;i<23;i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,19+4);
  UDP.endPacket();

  while (dur<5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,512);
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i=0;i<l;i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif DEBUG_S
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
        tnfs_fd=tnfsPacket.data[1];
#ifdef DEBUG_S
        BUG_UART.print("Successful, file descriptor: #");
        BUG_UART.println(tnfs_fd, HEX);
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // unsuccessful
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;  
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

public:

  void begin()
  {
    UDP.begin(TNFS_PORT);
    tnfs_mount();
    tnfs_open();
  }

  size_t write(uint8_t) 
  {return 0;}
  int available()   {return 0;}
  int read()   {return 0;}
  int peek()   {return 0;}
  void flush()  {}


  /*
  
     virtual size_t write(uint8_t) = 0;

  
     virtual int available() = 0;

     virtual int read() = 0;

     virtual int peek() = 0;


     virtual void flush() = 0;

   */
};

tnfsClient myTNFS;

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
   ISR for falling COMMAND
*/
void ICACHE_RAM_ATTR sio_isr_cmd()
{
  if (digitalRead(PIN_CMD) == LOW)
  {
    cmdState = ID;
    cmdTimer = millis();
  }
}

/**
   Get ID
*/
void sio_get_id()
{
  cmdFrame.devic = SIO_UART.read();
  if (cmdFrame.devic == 0x31)
    cmdState = COMMAND;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG_S
  BUG_UART.print("CMD DEVC: ");
  BUG_UART.println(cmdFrame.devic, HEX);
#endif
}

/**
   Get Command
*/
/*
void sio_get_command()
{
  cmdFrame.comnd = SIO_UART.read();
  if (cmdFrame.comnd == 'S' && statusSkipCount > STATUS_SKIP)
    cmdState = AUX1;
  else if (cmdFrame.comnd == 'S' && statusSkipCount < STATUS_SKIP)
  {
    statusSkipCount++;
    cmdState = WAIT;
    cmdTimer = 0;
  }
  else if (cmdFrame.comnd == 'R')
    cmdState = AUX1;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG_S
  BUG_UART.print("CMD CMND: ");
  BUG_UART.println(cmdFrame.comnd, HEX);
#endif
}
*/

void sio_get_command()
{
  cmdFrame.comnd = SIO_UART.read();
  if (cmdFrame.comnd == 'R' || cmdFrame.comnd == 'S' )
    cmdState = AUX1;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG_S
  BUG_UART.print("CMD CMND: ");
  BUG_UART.println(cmdFrame.comnd, HEX);
#endif
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  cmdFrame.aux1 = SIO_UART.read();
  cmdState = AUX2;

#ifdef DEBUG_S
  BUG_UART.print("CMD AUX1: ");
  BUG_UART.println(cmdFrame.aux1, HEX);
#endif
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  cmdFrame.aux2 = SIO_UART.read();
  cmdState = CHECKSUM;

#ifdef DEBUG_S
  BUG_UART.print("CMD AUX2: ");
  BUG_UART.println(cmdFrame.aux2, HEX);
#endif
}

/**
   Get Checksum, and compare
*/
void sio_get_checksum()
{
  byte ck;
  cmdFrame.cksum = SIO_UART.read();
  ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

#ifdef DEBUG_S
    BUG_UART.print("CMD CKSM: ");
    BUG_UART.print(cmdFrame.cksum, HEX);
#endif

    if (ck == cmdFrame.cksum)
    {
#ifdef DEBUG_S
      BUG_UART.println(", ACK");
#endif
      sio_ack();
    }
    else
    {
#ifdef DEBUG_S
      BUG_UART.println(", NAK");
#endif
      sio_nak();
    }
}

/**
   Process command
*/

void sio_process()
{
  switch (cmdFrame.comnd)
  {
    case 'R':
      sio_read();
      break;
    case 'S':
      sio_status();
      break;
  }
  
  cmdState = WAIT;
  cmdTimer = 0;
}

/**
   Read
*/
void sio_read()
{
  byte ck;
  byte sector[128];
  int offset =(256 * cmdFrame.aux2)+cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16; // skip 16 byte ATR Header
  tnfs_seek(offset);
  tnfs_read();

  for (int i=0;i<128;i++)
    sector[i]=tnfsPacket.data[i+3];

  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector,128);
    
  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG_S
  BUG_UART.print("SIO READ OFFSET: ");
  BUG_UART.print(offset);
  BUG_UART.print(" - ");
  BUG_UART.println((offset + 128));
#endif
}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
  byte ck;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  SIO_UART.write('A');
  SIO_UART.flush();
  //cmdState = PROCESS;
  sio_process();
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  cmdState = WAIT;
  cmdTimer = 0;
}

void sio_incoming(){
  switch (cmdState)
  {
    case ID:
      sio_get_id();
      break;
    case COMMAND:
      sio_get_command();
      break;
    case AUX1:
      sio_get_aux1();
      break;
    case AUX2:
      sio_get_aux2();
      break;
    case CHECKSUM:
      sio_get_checksum();
      break;
    case ACK:
      sio_ack();
      break;
    case NAK:
      sio_nak();
      break;
    case PROCESS:
      sio_process();
      break;
    case WAIT:
      SIO_UART.read(); // Toss it for now
      cmdTimer = 0;
      break;
  }
}

/**
 * TNFS read
 */
void tnfs_read()
{
  int start=millis();
  int dur=millis()-start;
  tnfsPacket.retryCount++;  // Increase sequence
  tnfsPacket.command=0x21;  // READ
  tnfsPacket.data[0]=tnfs_fd; // returned file descriptor
  tnfsPacket.data[1]=0x80;  // 128 bytes
  tnfsPacket.data[2]=0x00;  //

#ifdef DEBUG_S
  BUG_UART.print("Reading from File descriptor: ");
  BUG_UART.println(tnfs_fd);
  BUG_UART.print("Req Packet: ");
  for (int i=0;i<7;i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,4+3);
  UDP.endPacket();

  while (dur<5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,sizeof(tnfsPacket.rawData));
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i=0;i<l;i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
#ifndef DEBUG_S
        BUG_UART.println("Successful.");
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/        
        return;
      }
    }
  }
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
 * TNFS seek
 */
void tnfs_seek(long offset)
{
  int start=millis();
  int dur=millis()-start;
  byte offsetVal[4];

  // This may be sending the bytes in the wrong endian, pls check. Easiest way is to flip the indices.
  offsetVal[0] = (int)((offset & 0xFF000000) >> 24 );
  offsetVal[1] = (int)((offset & 0x00FF0000) >> 16 );
  offsetVal[2] = (int)((offset & 0x0000FF00) >> 8 );
  offsetVal[3] = (int)((offset & 0X000000FF));  
  
  tnfsPacket.retryCount++;
  tnfsPacket.command=0x25; // LSEEK
  tnfsPacket.data[0]=tnfs_fd;
  tnfsPacket.data[1]=0x00; // SEEK_SET
  tnfsPacket.data[2]=offsetVal[3];
  tnfsPacket.data[3]=offsetVal[2];
  tnfsPacket.data[4]=offsetVal[1];
  tnfsPacket.data[5]=offsetVal[0];

#ifdef DEBUG_S
  BUG_UART.print("Seek requested to offset: ");
  BUG_UART.println(offset);
  BUG_UART.print("Req packet: ");
  for (int i=0;i<10;i++)
  {
    BUG_UART.print(tnfsPacket.rawData[i], HEX);
    BUG_UART.print(" ");
  }
  BUG_UART.println("");
#endif /* DEBUG_S*/

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,6+4);
  UDP.endPacket();

  while (dur<5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,sizeof(tnfsPacket.rawData));
#ifdef DEBUG_S
      BUG_UART.print("Resp packet: ");
      for (int i=0;i<l;i++)
      {
        BUG_UART.print(tnfsPacket.rawData[i], HEX);
        BUG_UART.print(" ");
      }
      BUG_UART.println("");
#endif /* DEBUG_S */

      if (tnfsPacket.data[0]==0)
      {
        // Success.
#ifdef DEBUG_S
        BUG_UART.println("Successful.");
#endif /* DEBUG_S */
        return;  
      }
      else
      {
        // Error.
#ifdef DEBUG_S
        BUG_UART.print("Error code #");
        BUG_UART.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/        
        return;  
      }
    }
  }
#ifdef DEBUG_S
  BUG_UART.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

void setup() 
{
  // Set up pins
#ifdef DEBUG_S
  BUG_UART.begin(115200);
  BUG_UART.println();
  BUG_UART.println("#AtariWifi Test Program #7 started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
  pinMode(PIN_INT, INPUT);
  pinMode(PIN_PROC, INPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);

  WiFi.begin("SSID", "PASSWORD");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(10);
  }
  
  //  UDP.begin(16384);
  myTNFS.begin();
  // tnfs_mount();
  // tnfs_open();

  // Set up serial
  SIO_UART.begin(19200);
  //SIO_UART.swap();

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state
}

void loop() 
{
  if (SIO_UART.available() > 0)
  {
    sio_incoming();
  }
  
  if (millis() - cmdTimer > CMD_TIMEOUT && cmdState != WAIT)
  {
    BUG_UART.print("SIO CMD TIMEOUT: ");
    BUG_UART.println(cmdState);
    cmdState = WAIT;
    cmdTimer = 0;
  }
}
