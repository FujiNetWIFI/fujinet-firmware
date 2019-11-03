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

// Debugging bits.
// Global debug object.
RemoteDebug Debug;
const char* ssid = "Cherryhomes";
const char* password = "e1xb64XC46";

String hostNameWifi = MDNS_HOST_NAME;

void sio_cmd_change()
{
  debugI("SIO COMMAND change");
  while (Serial.available())
    {
      debugI("D: %d",Serial.read());
    }
}

void setup() {
  WiFi.begin(ssid, password);
  Serial.begin(19200);
  
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
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_cmd_change, CHANGE);
  debugI("setup complete.");
}

void loop() {
  // put your main code here, to run repeatedly:
  Debug.handle();
  yield();
}

#define HOST_NAME      "remotedebug"
#define MDNS_HOST_NAME "remotedebug.local"

#define PIN_LED        2
#define PIN_CMD        12
