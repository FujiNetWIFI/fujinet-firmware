#include <FS.h>

/**
   SIO test #4
*/

#define PIN_LED        2
#define PIN_CMD        12

File atr;
bool cmd; // inside command frame?

struct _cmdFrame
{
  unsigned char devic;
  unsigned char comnd;
  unsigned char aux1;
  unsigned char aux2;
  unsigned char cksum;
} cmdFrame;

byte buf[512];
int pos;

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
   Interrupt routine called when
*/
void sio_cmd()
{
  switch (digitalRead(PIN_CMD))
  {
    case HIGH:
      cmd = false;
      break;
    case LOW:
      cmd = true;
      break;
  }

  pos = 0;
  Serial.flush();
}

/**
   drive commands
*/
void drive_read()
{
  byte ck;
  byte sector[128];
  int offset = cmdFrame.aux1 + 256 * cmdFrame.aux2;
  offset *= 128;

  atr.seek(offset, SeekSet);
  atr.read(sector, 128);

  ck = sio_checksum((byte *)&sector, 128);

  // Completed command.
  Serial.write('C');
  delay(1);

  // Write data frame
  for (int i = 0; i < 128; i++)
    Serial.write(sector[i]);

  // Write data frame checksum
  Serial.write(ck);
  delay(1);
}

void drive_status()
{
  byte status[4];
  byte ck;

  status[0] = 0x00;
  status[1] = 0xFF;
  status[2] = 0xFE;
  status[3] = 0x00;

  ck = sio_checksum((byte *)&status, 4);

  Serial.write('C'); // Command always completes.
  delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    Serial.write(status[i]);

  // Write checksum
  Serial.write(ck);
}

/**
   Process drive commands
*/
void process_drive()
{
  switch (cmdFrame.comnd)
  {
    case 'R':
      drive_read();
      break;
    case 'S':
      drive_status();
      break;
  }
}

/**
   Process command
*/
void process_command()
{
  switch (cmdFrame.devic)
  {
    case 0x31:
      process_drive();
      break;
  }
}

/**
   Setup
*/
void setup()
{
  Serial.begin(19200);
  Serial.flush();
  Serial.swap();
  pinMode(PIN_CMD, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_cmd, CHANGE);
  atr = SPIFFS.open("/autorun.atr", "r");
}

void loop()
{
  byte ck;

  if (cmd == true)
  {
    // Try to get the command frame into buffer.
    while (pos < 5)
    {
      while (Serial.available() == 0) {  }
      buf[pos++] = Serial.read();
    }

    ck = sio_checksum((byte *)&buf, 4);

    if (ck == buf[4])
    {
      cmdFrame.devic = buf[0];
      cmdFrame.comnd = buf[1];
      cmdFrame.aux1 = buf[2];
      cmdFrame.aux2 = buf[3];
      cmdFrame.cksum = buf[4];
      Serial.write('A'); // ACK
      delay(1);
      process_command();
    }
    else
    {
      Serial.write('N'); // NAK
      delay(1);
    }
  }
}
