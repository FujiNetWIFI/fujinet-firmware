/**
 * Test #17 - CIO Test #3 - Let's have a Chat
 */

#include <ESP8266WiFi.h>
#include <FS.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug on 2nd UART (GPIO 2)
// #define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
#define DEBUG_N
#define DEBUG_HOST "192.168.1.7"


#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

File atr;

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

// This is the wificlient used by the SIO
WiFiClient sioclient;
WiFiServer sioserver(2000);
bool server_active;

#ifdef DEBUG_N
WiFiClient wificlient;
#endif

char packet[256];

#ifdef DEBUG
#define Debug_print(...) Debug_print( __VA_ARGS__ )
#define Debug_printf(...) Debug_printf( __VA_ARGS__ )
#define Debug_println(...) Debug_println( __VA_ARGS__ )
#define DEBUG
#endif
#ifdef DEBUG_N
#define Debug_print(...) wificlient.print( __VA_ARGS__ )
#define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
#define Debug_println(...) wificlient.println( __VA_ARGS__ )
#define DEBUG
#endif

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

#ifdef DEBUG
  Debug_print("CMD DEVC: ");
  Debug_println(cmdFrame.devic, HEX);
#endif
}

void sio_get_command()
{
  cmdFrame.comnd = Serial.read();
  cmdState=AUX1;

#ifdef DEBUG
  Debug_print("CMD CMND: ");
  Debug_println(cmdFrame.comnd, HEX);
#endif
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  cmdFrame.aux1 = Serial.read();
  cmdState = AUX2;

#ifdef DEBUG
  Debug_print("CMD AUX1: ");
  Debug_println(cmdFrame.aux1, HEX);
#endif
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  cmdFrame.aux2 = Serial.read();
  cmdState = CHECKSUM;

#ifdef DEBUG
  Debug_print("CMD AUX2: ");
  Debug_println(cmdFrame.aux2, HEX);
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

#ifdef DEBUG
    Debug_print("CMD CKSM: ");
    Debug_print(cmdFrame.cksum, HEX);
#endif

    if (ck == cmdFrame.cksum)
    {
#ifdef DEBUG
      Debug_println(", ACK");
#endif
      sio_ack();
    }
    else
    {
#ifdef DEBUG
      Debug_println(", NAK");
#endif
      sio_nak();
    }
}

/**
 * Accept a connection
 */
void sio_accept_connection()
{
  byte status;
  byte ck;
  
  sioclient=sioserver.available();

  if (sioclient)
  {
    status=1;  
  }
  else
  {
    status=255;  
  }

  ck = sio_checksum((byte *)&status, 1);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Command always completes.
  Serial.flush();
  delayMicroseconds(200);

  Serial.write(status);

  // Write checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
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
    case 'c':
      sio_tcp_connect();
      break;
    case 'd':
      sio_tcp_disconnect();
      break;
    case 'r':
      sio_tcp_read();
      break;
    case 's':
      sio_tcp_status();
      break;
    case 'a':
      sio_accept_connection();
      break;
    case 'w':
      sio_tcp_write();
      break;
    case 'l':
      sio_tcp_listen();
      break;
  }
  
  cmdState = WAIT;
  cmdTimer = 0;
}

/**
 * TCP read
 */
void sio_tcp_read()
{
  byte ck;
  
  memset(&packet,0x00,sizeof(packet));
  
  for (int i=0;i<cmdFrame.aux1;i++)
  {
    packet[i]=sioclient.read();
    if (packet[i]==-1)
      break;
  }

  ck = sio_checksum((byte *)&packet, cmdFrame.aux1);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Completed command
  Serial.flush();

  // Write data frame
  Serial.write(packet,cmdFrame.aux1);
    
  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
}

/**
 * TCP status
 */
void sio_tcp_status()
{
  int available;
  byte status[4];
  byte ck;

  available=sioclient.available();

  status[0]=available&0xFF;
  status[1]=available>>8;
  status[2]=WiFi.status();
  status[3]=0x00;

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
#ifdef DEBUG
  Debug_print("SIO READ OFFSET: ");
  Debug_print(offset);
  Debug_print(" - ");
  Debug_println((offset + 128));
#endif
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
    Debug_printf("Network Status 0x02x\n\n",status[0]);
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
 * SIO Connect TCP
 */
void sio_tcp_connect(void)
{
  byte ck;
  char* thn;
  char* tpn; // hostname and port # tokens
  int port;

  memset(&packet, 0x00, sizeof(packet));
  
#ifdef DEBUG
  Debug_println("Receiving 256b frame from computer");
#endif

  Serial.readBytes(packet, 256);
  ck = Serial.read(); // Read checksum

  if (ck != sio_checksum((byte *)&packet, 256))
  {
#ifdef DEBUG
    Debug_println("Bad Checksum");
#endif
    Serial.write('N'); // NAK
    return;
  }
#ifdef DEBUG
  Debug_println("ACK");
#endif
  Serial.write('A');   // ACK

  // Tokenize the connection string.
  thn=strtok(packet,":");
  tpn=strtok(NULL,":");
  port=atoi(tpn);

#ifdef DEBUG
  Debug_printf("thn: %s tpn: %s port %d",thn,tpn,port);
#endif

  if (sioclient.connect(thn,port)==true)
  {
#ifdef DEBUG
    Debug_print("COMPLETE");
#endif
    sioclient.write(0x0D);
    Serial.write('C');
  }
  else
  {
#ifdef DEBUG
    Debug_print("ERROR");
#endif
    Serial.write('E');
  }  
}

/**
 * SIO Disconnect TCP
 */
void sio_tcp_disconnect(void)
{
  byte ck;

  memset(&packet, 0x00, sizeof(packet));
  
#ifdef DEBUG
  Debug_println("Receiving 1b frame from computer");
#endif

  Serial.readBytes(packet, 1);
  ck = Serial.read(); // Read checksum

  if (ck != sio_checksum((byte *)&packet, 1))
  {
    Serial.write('N'); // NAK
    return;
  }

  Serial.write('A');   // ACK
  sioclient.stop();
  
  Serial.write('C');
}

/**
 * SIO Write TCP string
 */
 void sio_tcp_write(void)
{
  byte ck;
  
#ifdef DEBUG
  Debug_printf("Receiving %d bytes frame from computer",cmdFrame.aux1);
#endif

  memset(&packet, 0x00, sizeof(packet));

  Serial.readBytes(packet, cmdFrame.aux1);
  ck = Serial.read(); // Read checksum

  if (ck != sio_checksum((byte *)&packet, cmdFrame.aux1))
  {
    Serial.write('N'); // NAK
    return;
  }

  Serial.write('A');   // ACK

#ifdef DEBUG
  Debug_printf("Writing %d bytes to computer.",cmdFrame.aux1);
#endif

  for (int i=0;i<cmdFrame.aux1;i++)
  {
    sioclient.write(packet[i]);
    Debug_printf("%02x ",packet[i]);
  }
  
  Serial.write('C');
}

/**
 * SIO TCP listen
 */
void sio_tcp_listen(void)
{
  int port;
  byte ck;
  
#ifdef DEBUG
  Debug_printf("Receiving 5 bytes frame from computer");
#endif

  memset(&packet, 0x00, sizeof(packet));

  Serial.readBytes(packet, 5);
  ck = Serial.read(); // Read checksum

  if (ck != sio_checksum((byte *)&packet, 5))
  {
    Serial.write('N'); // NAK
    return;
  }

  Serial.write('A');   // ACK

  port=atoi(packet);

#ifdef DEBUG
  Debug_printf("Now listening for connections on port 2000\n");
#endif

  sioserver.begin();
  
  Serial.write('C');

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

void setup() 
{
  SPIFFS.begin();
  atr=SPIFFS.open("/autorun.atr","r");
  // Set up pins
#ifdef DEBUG_S
  Serial1.begin(19200);
  Debug_println();
  Debug_println("#FujiNet CIO Test #16 started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer.
  pinMode(PIN_PROC, OUTPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);

  WiFi.begin("Cherryhomes", "e1xb64XC46");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(10);
  }  

#ifdef DEBUG_N
    wificlient.connect(DEBUG_HOST, 6502);
    wificlient.println("#AtariWifi CIO Test");
#endif
  
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
    Debug_print("SIO CMD TIMEOUT: ");
    Debug_println(cmdState);
    cmdState = WAIT;
    cmdTimer = 0;
  } 
}
