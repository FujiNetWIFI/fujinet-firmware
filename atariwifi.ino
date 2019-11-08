/**
   SIO Test #5 - Implement as a FSM
*/

#include <FS.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12

#define DELAY_T5       600

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
  }
  else
  {
    cmdState = WAIT;
  }
}

/**
   Get ID
*/
void sio_get_id()
{
  while (Serial.available() == 0) { }
  if (Serial.available() > 0)
    cmdFrame.devic = Serial.read();
  {
    if (cmdFrame.devic == 0x31)
      cmdState = COMMAND;
    else
      cmdState = WAIT;
  }
}

/**
   Get Command
*/
void sio_get_command()
{
  while (Serial.available() == 0) { }
  if (Serial.available() > 0)
  {
    cmdFrame.comnd = Serial.read();
    cmdState = AUX1;
  }
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  while (Serial.available() == 0) { }
  if (Serial.available() > 0)
  {
    cmdFrame.aux1 = Serial.read();
    cmdState = AUX2;
  }
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  while (Serial.available() == 0) { }
  if (Serial.available() > 0)
  {
    cmdFrame.aux2 = Serial.read();
    cmdState = CHECKSUM;
  }
}

/**
   Get Checksum, and compare
*/
void sio_get_checksum()
{
  byte ck;
  while (Serial.available() == 0) { }
  if (Serial.available() > 0)
  {
    cmdFrame.cksum = Serial.read();
    ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

    if (ck == cmdFrame.cksum)
    {
      cmdState = ACK;
    }
    else
    {
      cmdState = NAK;
    }
  }
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(800);
  Serial.write('A');
  Serial.flush();
  cmdState = PROCESS;
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(800);
  Serial.write('N');
  Serial.flush();
  cmdState = WAIT;
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
  atr.seek(offset, SeekSet);
  atr.read(sector, 128);

  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(600); // t5 delay
  Serial.write('C'); // Completed command

  // Write data frame
  for (int i = 0; i < 128; i++)
  {
    Serial.write(sector[i]);
  }

  // Write data frame checksum
  Serial.write(ck);
  delayMicroseconds(200);
}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
  byte ck;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(600); // t5 delay
  Serial.write('C'); // Command always completes.
  delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    Serial.write(status[i]);

  // Write checksum
  Serial.write(ck);
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
  }
  cmdState = WAIT;
}

void setup()
{
  SPIFFS.begin();
  atr=SPIFFS.open("/autorun.atr","r");
  
  // Set up pins
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_INT, INPUT);
  pinMode(PIN_PROC, INPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);

  // Set up serial
  Serial.begin(19200);
  Serial.swap();

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, CHANGE);
}

void loop()
{
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
      break;
  }
}
