#include <ESP8266WiFi.h>

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
#define DEBUG_N
#define DEBUG_HOST "192.168.1.7"

#ifdef DEBUG_N
WiFiClient wificlient;
#endif

#ifdef DEBUG_N
#define Debug_print(...) wificlient.print( __VA_ARGS__ )
#define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
#define Debug_println(...) wificlient.println( __VA_ARGS__ )
#define DEBUG
#endif

void setup() {
  int numNetworks, i;
  WiFi.begin("SSID", "PASSWORD");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(10);
  }
  
  // put your setup code here, to run once:
#ifdef DEBUG_N
  wificlient.connect(DEBUG_HOST, 6502);
  wificlient.println("#FujiNet Wifi Scan Debug");
#endif

#ifdef DEBUG_N
  Debug_printf("Scanning for networks...\n\n");
  WiFi.mode(WIFI_STA);
  numNetworks=WiFi.scanNetworks();
  for (i=0;i<numNetworks;i++)
  {
    Debug_printf("%d: %s %d\n",i,WiFi.SSID(i).c_str(),WiFi.RSSI(i));
  }
  Debug_printf("\n\nScan complete.\n");
#endif

}

void loop() {
  // put your main code here, to run repeatedly:

}
