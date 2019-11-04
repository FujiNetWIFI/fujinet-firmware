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
 * Atari WIFI Modem Firmware
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
int cmdFrame[5]; // Holds the command frame data
int cmdFramePos = 0; // Position in command frame array
unsigned long cmdTimeoutInterval; // max wait time for cmd frame

void sio_cmd_change()
{
  int pinState = digitalRead(PIN_CMD);
  if (pinState == 0) // low indicates incoming cmd frame
    {
      incomingSioCmd = true;
      cmdTimeoutInterval = millis();
    }
    else if(pinState == 1) // back to high, cmd frame over
    {
      incomingSioCmd = false;
      cmdFramePos = 0;
    }
}

void sio_cmd_start()
{
  incomingSioCmd = true;
  cmdTimeoutInterval = millis();
}

void setup() {
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

void loop() {
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
          if (cmdFramePos == 0){
              if((cmdFrame[cmdFramePos] > 47 && cmdFrame[cmdFramePos] < 56) // Disk drives
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
            else
            {
              debugI("C%d: 0x%X", cmdFramePos, cmdFrame[cmdFramePos]); // Print in HEX
              cmdFramePos++;
            }
        }
      if (digitalRead(PIN_CMD) == HIGH && incomingSioCmd == true)
        {
          debugI("SIO COMMAND end (pin HIGH)");
          incomingSioCmd = false;
          cmdFramePos = 0;
        }
     }

  //yield(); // Not needed until our program gets bigger. This is automatically called every loop
}
