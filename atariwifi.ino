#include <FS.h>
#include <ESP8266mDNS.h>

#include <RemoteDebug.h>
#include <RemoteDebugCfg.h>
#include <RemoteDebugWS.h>
#include <telnet.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiType.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>

#include <dummy.h>

/**
   Atari WIFI Modem Firmware
*/

#define HOST_NAME      "remotedebug"
#define MDNS_HOST_NAME "remotedebug.local"

#define PIN_LED        2
#define PIN_CMD        12

#define READ_CMD_TIMEOUT  500

// Debugging bits.
// Global debug object.
RemoteDebug Debug;
const char* ssid = "";
const char* password = "";

String hostNameWifi = MDNS_HOST_NAME;

bool incomingSioCmd = false;
bool validCommand = false;
int cmdFrame[5]; // Holds the command frame data
int cmdFramePos = 0; // Position in command frame array
unsigned long cmdTimeoutInterval; // max wait time for cmd frame
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
   interrupt when SIO line lowers.
*/
void sio_cmd_change()
{
  int pinState = digitalRead(PIN_CMD);
  if (pinState == 0) // low indicates incoming cmd frame
  {
    incomingSioCmd = true;
    cmdTimeoutInterval = millis();
    Serial.flush();
  }
  else if (pinState == 1) // back to high, cmd frame over
  {
    incomingSioCmd = false;
    cmdFramePos = 0;
    Serial.flush();
  }
}

void sio_cmd_start()
{
  incomingSioCmd = true;
  validCommand = false;
  cmdTimeoutInterval = millis();
}

void setup() {
  SPIFFS.begin();
  atr = SPIFFS.open("autorun.atr","r");
  WiFi.begin(ssid, password);
  Serial.begin(19200);
  Serial.flush();
  Serial.swap();

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  WiFi.hostname(MDNS_HOST_NAME);
  MDNS.begin(HOST_NAME);
  MDNS.addService("telnet", "tcp", 23);
  Debug.begin(HOST_NAME);
  Debug.setResetCmdEnabled(true); // Enable the reset command
  Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
  pinMode(PIN_CMD, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_cmd_start, FALLING);
  debugI("setup complete.");
}

/**
   Drive status
*/
void process_drive_status()
{
  byte status[4];
  byte ck;
  debugI("STATUS");
  status[0] = 0x00; // Last sio status
  status[1] = 0xFF; // Inverted 1771 status
  status[2] = 0xFE; // Format timeout;
  status[3] = 0x00; // Reserved

  ck = sio_checksum((byte *)&status, 4);

  Serial.write('C'); // Indicate command complete
  delay(1);

  // write data frame
  for (int i = 0; i < 4; i++)
    Serial.write(status[i]);

  // Write data frame checksum
  Serial.write(ck);
}

/**
   Drive read
*/
void process_drive_read()
{
  byte ck;
  byte sector[128];
  int offset;
  
  debugI("READ");

  offset=(cmdFrame[3]*256)+cmdFrame[2]+16; // 16 byte ATR header.
  atr.seek(offset,SeekSet);

  atr.read(sector,128);

  ck = sio_checksum((byte *)&sector, 128);

  Serial.write('C'); // Indicate command complete
  delay(1);

  // Write data frame
  for (int i = 0; i < 128; i++)
    Serial.write(sector[i]);

  // Write data frame checksum
  Serial.write(ck);
}

/**
   Process a drive command
*/
void process_drive_command()
{
  switch (cmdFrame[1])
  {
    case 'S':
      process_drive_status();
      break;
    case 'R':
      process_drive_read();
      break;
  }
}

/**
   Process a valid command
*/
void process_command() {
  debugI("process_command()");
  switch (cmdFrame[0])
  {
    case 0x31:
      process_drive_command();
      break;
    default:
      debugI("Not processing command for SIO ID: 0x%02x", cmdFrame[0]);
      break;
  }
}

void loop() {
  byte cksum;
  Debug.handle();
  if (incomingSioCmd == true)
  {
    if (millis() - cmdTimeoutInterval > READ_CMD_TIMEOUT)
    {
      debugI("SIO COMMAND timeout");
      incomingSioCmd = false;
      cmdFramePos = 0;
    }
    if (cmdFramePos > 4)
    {
      debugI("SIO COMMAND end");
      incomingSioCmd = false;
      cmdFramePos = 0;
    }
    if (cmdFramePos == 0 && incomingSioCmd == true)
      debugI("SIO COMMAND start");

    if (Serial.available() > 0 && incomingSioCmd == true)
    {
      cmdFrame[cmdFramePos] = Serial.read();
      if (cmdFramePos == 0) {
        if ((cmdFrame[cmdFramePos] > 47 && cmdFrame[cmdFramePos] < 56) // Disk drives
            || (cmdFrame[cmdFramePos] > 79 && cmdFrame[cmdFramePos] < 84)) // R device
        {
          // Continue if valid device id
          debugI("C%d: 0x%X", cmdFramePos, cmdFrame[cmdFramePos]); // Print in HEX
          cmdFramePos++;
        }
        else
        {
          // Invalid Device ID, ignore the rest of this command
          debugI("SIO COMMAND Invalid DeviceID: 0x%X", cmdFrame[cmdFramePos]);
          incomingSioCmd = false;
          cmdFramePos = 0;
        }
      }
      else if (cmdFramePos == 4)
      {
        if ((cmdFrame[1] > 47 && cmdFrame[cmdFramePos] < 56) || (cmdFrame[1] > 79 && cmdFrame[cmdFramePos] < 84))
        {
          cksum = sio_checksum((byte *)&cmdFrame, 4);
          if (cksum == cmdFrame[4])
          {
            debugI("C%d: 0x%X ACK!", cmdFramePos, cmdFrame[cmdFramePos]);
            Serial.write('A');
            validCommand = true;
            cmdFramePos++;
          }
          else
          {
            debugI("C%d: 0x%X NAK!", cmdFramePos, cmdFrame[cmdFramePos]);
            Serial.write('N');
            validCommand = false;
            cmdFramePos++;
          }
        }
      }
      else
      {
        debugI("C%d: 0x%X", cmdFramePos, cmdFrame[cmdFramePos]); // Print in HEX
        cmdFramePos++;
      }
    }
    if (digitalRead(PIN_CMD) == HIGH && incomingSioCmd == true)
    {
      debugI("SIO COMMAND end (pin HIGH)");
      if (validCommand == true)
        process_command();
      incomingSioCmd = false;
      validCommand = false;
      cmdFramePos = 0;
    }
  }

  //yield(); // Not needed until our program gets bigger. This is automatically called every loop
}
