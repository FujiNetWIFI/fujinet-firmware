/**
 * Test #8 - Return Network info via SIO
 */

// changes for esp32
// 1. change network library to WiFi.h
// 2. change file library to SPIFFS.h
// 3. define serial ports
// 4. renumber the pins
// 5. remove Serial.swap();
// 6. search and replace "Serial." with "SIO_UART"
// 7. search and replace "BUG_UART." with "BUG_UART"
// 8. change ISR label to IRAM_ATTR

#include <WiFi.h>
#include <SPIFFS.h>

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

File atr;

unsigned long cmdTimer = 0;
byte statusSkipCount = 0;

/**
 * The Netinfo structure to make it easy.
 *
 * ssid = the currently connected access point
 * bssid = the MAC address of the access point (in little endian order)
 * ipAddress = the IP address of the adapter (in little endian order)
 * macAddress = the MAC address of the adapter (in little endian order)
 * rssi = The calculated signal strength in dBm.
 */
union
{
  struct
  {
    char ssid[32];
    unsigned char bssid[6];
    long ipAddress;
    unsigned char macAddress[6];
    unsigned long rssi;
    unsigned char reserved[12];
  };
  unsigned char rawData[64];
} netInfo;

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
void IRAM_ATTR sio_isr_cmd()
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
  if (cmdFrame.devic == 0x31 || cmdFrame.devic==0x70)
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

void sio_get_command()
{
  cmdFrame.comnd = SIO_UART.read();
  if (cmdFrame.comnd == 'R' || cmdFrame.comnd == 'S' || cmdFrame.comnd=='!' )
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
    case '!':
      sio_send_net_info();
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
   Read
*/
void sio_send_net_info()
{
  byte ck;
  
  ck = sio_checksum((byte *)&netInfo.rawData, 64);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(netInfo.rawData,64);
    
  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG_S
  BUG_UART.print("SIO SEND NETWORK INFO. ");
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

void setup() 
{
  SPIFFS.begin();
  atr=SPIFFS.open("/autorun.atr","r");
  // Set up pins
#ifdef DEBUG_S
  BUG_UART.begin(115200);
  BUG_UART.println();
  BUG_UART.println("#AtariWifi Test Program #8 started");
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
    #ifdef DEBUG_S
    BUG_UART.println("no wifi");
    #endif
  }

  // Fill in netInfo structure.
  strcpy(netInfo.ssid,WiFi.SSID().c_str());
  netInfo.bssid[5]=WiFi.BSSID()[0];
  netInfo.bssid[4]=WiFi.BSSID()[1];
  netInfo.bssid[3]=WiFi.BSSID()[2];
  netInfo.bssid[2]=WiFi.BSSID()[3];
  netInfo.bssid[1]=WiFi.BSSID()[4];
  netInfo.bssid[0]=WiFi.BSSID()[5];
  netInfo.ipAddress=WiFi.localIP();
  netInfo.macAddress[5]=WiFi.macAddress()[0];
  netInfo.macAddress[4]=WiFi.macAddress()[1];
  netInfo.macAddress[3]=WiFi.macAddress()[2];
  netInfo.macAddress[2]=WiFi.macAddress()[3];
  netInfo.macAddress[1]=WiFi.macAddress()[4];
  netInfo.macAddress[0]=WiFi.macAddress()[5];
  netInfo.rssi=WiFi.RSSI();
  
  // Set up serial
  SIO_UART.begin(19200);
  // SIO_UART.swap();

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
