/**
   SIO Test #5 - Implement as a FSM
*/

#include <FS.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug on 2nd UART (GPIO 2)
// #define DEBUG_S

#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12

#define DELAY_T5          600
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

unsigned long cmdTimer = 0;
byte statusSkipCount = 0;

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
void sio_isr_cmd()
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
  if (cmdFrame.devic == 0x31)
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

/**
   Get Command
*/
void sio_get_command()
{
  cmdFrame.comnd = Serial.read();
  if (cmdFrame.comnd == 'S' && statusSkipCount >= STATUS_SKIP)
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

  delayMicroseconds(1500); // t5 delay
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
   Status
*/
void sio_status()
{
  byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
  byte ck;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(1500); // t5 delay
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
  Serial1.println("atariwifi started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
  pinMode(PIN_INT, INPUT);
  pinMode(PIN_PROC, INPUT);
  pinMode(PIN_MTR, INPUT_PULLDOWN_16);
  pinMode(PIN_CMD, INPUT);

  // Set up serial
  Serial.begin(19200);
  Serial.swap();

  // Attach COMMAND interrupt.
  //attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, CHANGE);
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
