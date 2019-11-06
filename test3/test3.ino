#include <dummy.h>

#include <FS.h>

#define PIN_CMD 12
#define PIN_LED 2

byte cmd_devid;
byte cmd_dcomnd;
byte cmd_daux1;
byte cmd_daux2;
byte cmd_cksum;
byte cmd_available=false;
File atr;

/////////////////////////////////////////////////

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
 * COMMAND line has lowered, immediately read command!
 */
 void sio_cmd_lower()
{
  if (digitalRead(PIN_CMD)==LOW)
  {
    delay(1);
    cmd_devid=Serial.read();
    cmd_dcomnd=Serial.read();
    cmd_daux1=Serial.read();
    cmd_daux2=Serial.read();
    cmd_cksum=Serial.read();
    cmd_available=true;
  }
}

void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_LED,OUTPUT);
  digitalWrite(PIN_LED,HIGH);
  SPIFFS.begin();
  atr = SPIFFS.open("autorun.atr","r");
  Serial.begin(19200);
  Serial.swap();
  Serial.flush();
  attachInterrupt(digitalPinToInterrupt(PIN_CMD),sio_cmd_lower,CHANGE);
}

/**
 * Process a drive read
 */
void sio_process_drive_read()
{
  byte ck;
  byte sector[128];
  int offset;

  offset=(cmd_daux2*256)+cmd_daux1+16; // 16 byte ATR header.
  atr.seek(offset,SeekSet);

  atr.read(sector,128);

  ck = sio_checksum((byte *)&sector, 128);

  Serial.write('C');
  delay(1);

  for (int i=0; i<128; i++)
    Serial.write(sector[i]);

  Serial.write(ck);
}

/**
 * Process a drive status
 */
void sio_process_drive_status()
{
  byte status[4];
  byte ck;

  digitalWrite(PIN_LED,LOW);

  status[0]=0x00;
  status[1]=0xFF;
  status[2]=0xFE;
  status[3]=0x00;
  ck=sio_checksum((byte *)&status, 4);
  Serial.write('C');

  delay(1);

  for (int i=0; i<4; i++)
    Serial.write(status[i]);

  Serial.write(ck);  
}

/**
 * Process a drive command
 */
byte sio_process_drive_command()
{
  switch (cmd_dcomnd)
  {
    case 'R':  // READ
      sio_process_drive_read();
      break;
    case 'S':
      sio_process_drive_status();
      break;
  }
}

/**
 * Process a received command
 */
void sio_process_command()
{
  byte r;

  switch (cmd_devid)
  {
    case 0x31: // drive 1
      Serial.write('A');
      delay(1);
      sio_process_drive_command();
      break;
  }
  cmd_available=false;
}

/**
 * Primary loop.
 */
void loop() {
  //if (cmd_available==true)
    sio_process_command();
    
}
