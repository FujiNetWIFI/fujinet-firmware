/**
 * Test #13 - Example SIO to CIO call
 */

#include <ESP8266WiFi.h>
#include <FS.h>

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
    case 'Y':
      sio_hello_cio();
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
   Example SIO to CIO call
*/
void sio_hello_cio()
{
  byte ck;
  char output[20];
  output[0]='H';
  output[1]='e';
  output[2]='l';
  output[3]='l';
  output[4]='o';
  output[5]=' ';
  output[6]='f';
  output[7]='r';
  output[8]='o';
  output[9]='m';
  output[10]=' ';
  output[11]='F';
  output[12]='u';
  output[13]='j';
  output[14]='i';
  output[15]='N';
  output[16]='e';
  output[17]='t';
  output[18]='!';
  output[19]=0x9B; // ATASCII EOL
  
  ck = sio_checksum((byte *)&output, 20);

  delayMicroseconds(DELAY_T5); // t5 delay
  
  Serial.write('C'); // Completed command
  delayMicroseconds(300);
  // Write data frame
  Serial.write(output,20);
    
  // Write data frame checksum
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

void setup() 
{
  SPIFFS.begin();
  atr=SPIFFS.open("/autorun.atr","r");
  // Set up pins
#ifdef DEBUG_S
  Serial1.begin(19200);
  Serial1.println();
  Serial1.println("#AtariWifi Test Program #12 started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer.
  pinMode(PIN_PROC, OUTPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
  
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
