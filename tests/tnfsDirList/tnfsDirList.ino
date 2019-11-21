/**
 * Test #12 - TNFS List Directory
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <FS.h>

#define TNFS_SERVER "192.168.1.7"
#define TNFS_PORT 16384

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug on 2nd UART (GPIO 2)
// #define DEBUG_S

#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

WiFiUDP UDP;
File atr;
File dircache;

byte tnfs_fd;
byte tnfs_dir_fd;
byte current_entry[256];
int num_entries;
int entry_index;

unsigned long cmdTimer = 0;

/**
 * A Single command frame, both in structured and unstructured
 * form.
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
  cmdFrame.devic = Serial.read();
  if (cmdFrame.devic == 0x31 || cmdFrame.devic==0x70)
    cmdState = COMMAND;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG_S
  Serial1.print("CMD DEVC: ");
  Serial1.println(cmdFrame.devic, HEX);
#endif
}

void sio_get_command()
{
  cmdFrame.comnd = Serial.read();
  cmdState=AUX1;

#ifdef DEBUG_S
  Serial1.print("CMD CMND: ");
  Serial1.println(cmdFrame.comnd, HEX);
#endif
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  cmdFrame.aux1 = Serial.read();
  cmdState = AUX2;

#ifdef DEBUG_S
  Serial1.print("CMD AUX1: ");
  Serial1.println(cmdFrame.aux1, HEX);
#endif
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  cmdFrame.aux2 = Serial.read();
  cmdState = CHECKSUM;

#ifdef DEBUG_S
  Serial1.print("CMD AUX2: ");
  Serial1.println(cmdFrame.aux2, HEX);
#endif
}

/**
   Get Checksum, and compare
*/
void sio_get_checksum()
{
  byte ck;
  cmdFrame.cksum = Serial.read();
  ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

#ifdef DEBUG_S
    Serial1.print("CMD CKSM: ");
    Serial1.print(cmdFrame.cksum, HEX);
#endif

    if (ck == cmdFrame.cksum)
    {
#ifdef DEBUG_S
      Serial1.println(", ACK");
#endif
      sio_ack();
    }
    else
    {
#ifdef DEBUG_S
      Serial1.println(", NAK");
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
    case '$': // Set path
      sio_open_directory();
      break;
    case '%': // Read dir entry.
      sio_read_dir_entry();
      break;
    case '^': // close dir.
      sio_close_directory();
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

  atr.seek(offset, SeekSet);
  atr.read(sector, 128);

  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Completed command
  Serial.flush();

  // Write data frame
  Serial.write(sector,128);
    
  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
#ifdef DEBUG_S
  Serial1.print("SIO READ OFFSET: ");
  Serial1.print(offset);
  Serial1.print(" - ");
  Serial1.println((offset + 128));
#endif
}

/**
 * SIO close dir
 */
void sio_close_directory()
{
  delayMicroseconds(DELAY_T5);
  tnfs_closedir();
  Serial.write('C'); // Completed command
  Serial.flush();
}

/**
 * SIO set path
 */
void sio_open_directory()
{
  byte ck;

#ifndef DEBUG_S
  Serial1.printf("Receiving 256b frame from computer");
#endif

  Serial.readBytes(current_entry,256);
  ck=Serial.read(); // Read checksum

  if (ck!=sio_checksum(current_entry,256))
  {
    Serial.write('N'); // NAK
    return;  
  }

  Serial.write('A');   // ACK

  tnfs_opendir();
  
  // And complete.
  Serial.write('C');
}

/**
 * SIO read dir entry
 */
void sio_read_dir_entry()
{
  byte ck;
  long offset;
  byte ret;
  
  memset(current_entry,0x00,256);

  tnfs_readdir();
 
  ck = sio_checksum((byte *)&current_entry, 36);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Command always completes.
  Serial.flush();
  
  delayMicroseconds(200);

  // Write data frame
  Serial.write(current_entry,36);

  // Write checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
  byte ck;

  if (cmdFrame.devic==0x70)
  {
    // Network status is different
    memset(status,0x00,sizeof(status));
    status[0]=WiFi.status();
#ifndef DEBUG_S
    Serial1.printf("Network Status 0x02x\n\n",status[0]);
#endif
  }

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Command always completes.
  Serial.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    Serial.write(status[i]);

  // Write checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  Serial.write('A');
  Serial.flush();
  //cmdState = PROCESS;
  sio_process();
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  Serial.write('N');
  Serial.flush();
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
      Serial.read(); // Toss it for now
      cmdTimer = 0;
      break;
  }
}

/**
 * TNFS Open Directory
 */
void tnfs_opendir()
{
  int start=millis();
  int dur=millis()-start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command=0x10;  // OPENDIR
  tnfsPacket.data[0]='/';   // Open root dir
  tnfsPacket.data[1]=0x00;  // nul terminated

#ifdef DEBUG_S
  Serial1.println("TNFS Open directory /");
#endif

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,2+4);
  UDP.endPacket();

  while (dur<5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,512);
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
        tnfs_dir_fd=tnfsPacket.data[1];
        return;  
      }
      else
      {
        // Unsuccessful  
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG_S
  Serial1.println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
 * TNFS Read Directory
 * Reads the next directory entry
 */
bool tnfs_readdir()
{
  int start=millis();
  int dur=millis()-start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command=0x11;  // READDIR
  tnfsPacket.data[0]=tnfs_dir_fd;   // Open root dir

#ifdef DEBUG_S
  Serial1.println("TNFS Read next dir entry");
#endif

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,1+4);
  UDP.endPacket();

  while (dur<5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,512);
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
        strcpy((char*)&current_entry,(char *)&tnfsPacket.data[1]);
        return true;  
      }
      else
      {
        // Unsuccessful
        return false; 
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG_S
  Serial1.println("Timeout after 5000ms.");
#endif /* DEBUG_S */  
}

/**
 * TNFS Close Directory
 */
void tnfs_closedir()
{
  int start=millis();
  int dur=millis()-start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command=0x12;  // CLOSEDIR
  tnfsPacket.data[0]=tnfs_dir_fd;   // Open root dir

#ifdef DEBUG_S
  Serial1.println("TNFS dir close");
#endif

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,1+4);
  UDP.endPacket();

  while (dur<5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,512);
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
        return;  
      }
      else
      {
        // Unsuccessful
        return; 
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG_S
  Serial1.println("Timeout after 5000ms.");
#endif /* DEBUG_S */  
}


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
  Serial1.print("Mounting / from ");
  Serial1.println(TNFS_SERVER);
  Serial1.print("Req Packet: ");
  for (int i=0;i<10;i++)
  {
    Serial1.print(tnfsPacket.rawData[i], HEX);
    Serial1.print(" ");
  }
  Serial1.println("");
#endif /* DEBUG_S */

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,10);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket.rawData,512);
#ifdef DEBUG_S
      Serial1.print("Resp Packet: ");
      for (int i=0;i<l;i++)
      {
        Serial1.print(tnfsPacket.rawData[i], HEX);
        Serial.print(" ");
      }
      Serial1.println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0]==0x00)
      {
        // Successful
#ifdef DEBUG_S
        Serial1.print("Successful, Session ID: ");
        Serial1.print(tnfsPacket.session_idl, HEX);
        Serial1.println(tnfsPacket.session_idh, HEX);
#endif /* DEBUG_S */
        return;  
      }
      else
      {
        // Error
#ifdef DEBUG_S
        Serial1.print("Error #");
        Serial1.println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
        return;  
      }
    }
  }
  // Otherwise we timed out.
#ifdef DEBUG_S
Serial1.println("Timeout after 5000ms");
#endif /* DEBUG_S */
}

void setup() 
{
  SPIFFS.begin();
  atr=SPIFFS.open("/autorun.atr","r");
  // Set up pins
#ifdef DEBUG_S
  Serial1.begin(19200);
  Serial1.println();
  Serial1.println("#AtariWifi Test Program #11 started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer.
  pinMode(PIN_PROC, OUTPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);

  WiFi.begin("SSID","PASSWORD");
  
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(10);
  }
  
  UDP.begin(16384);

  tnfs_mount();
  
  // Set up serial
  Serial.begin(19200);
  Serial.swap();

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state
}

void loop() 
{
  if (Serial.available() > 0)
  {
    sio_incoming();
  }
  
  if (millis() - cmdTimer > CMD_TIMEOUT && cmdState != WAIT)
  {
    Serial1.print("SIO CMD TIMEOUT: ");
    Serial1.println(cmdState);
    cmdState = WAIT;
    cmdTimer = 0;
  } 
}
