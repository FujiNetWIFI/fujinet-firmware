/**
   Test #6 - See if we can make an ESP8266 talk TNFS!
*/

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

#define TNFS_SERVER "192.168.1.7"
#define TNFS_PORT 16384

enum {MOUNT, OPEN, READ, CLOSE, UMOUNT, DONE} tnfs_state = MOUNT;

WiFiUDP UDP;

union
{
  struct
  {
    unsigned short session_id;
    byte retryCount;
    byte command;
    byte data[1020];
  };
  byte rawData[1024];
} tnfsPacket;

int tnfsPacketLen=0;
byte tnfsReadfd;

/**
 * Setup.
 */
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("#AtariWiFi Test #6: TNFS client");
  Serial.print("Connecting to WiFi...");
  WiFi.begin("Cherryhomes", "e1xb64XC46");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected.");

  Serial.println("Initializing UDP.");
  UDP.begin(16384);
}

/**
 * Mount
 */
void tnfs_mount()
{
  int start=millis();
  int dur=millis()-start;
  int len=0;
  bool done=false;

  Serial.printf("Attempting mount of %s - Attempt #%d\n\n",TNFS_SERVER,tnfsPacket.retryCount);
  tnfsPacket.command=0x00; // Mount
  tnfsPacket.data[0]=0x00; // version 1.0 requested
  tnfsPacket.data[1]=0x01;
  tnfsPacket.data[2]='/';  // mount /
  tnfsPacket.data[3]=0x00; //  '' 
  tnfsPacket.data[4]=0x00; // no username
  tnfsPacket.data[5]=0x00; // nor password.

  // Send command.
  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,10);
  UDP.endPacket();

  while(done==false)
  {
    dur=millis()-start;
    if (dur>5000)
    {
      Serial.printf("Timeout. Retrying.");
      done=true;
    }
    else if (UDP.parsePacket())
    {
      UDP.read(tnfsPacket.rawData,1024);
      Serial.printf("Mounted /, session 0x%x\n",tnfsPacket.session_id);
      done=true;
      tnfs_state=OPEN;
    }
  }
}

/**
 * Open
 */
void tnfs_open()
{
  int start=millis();
  int dur=millis()-start;
  int len=0;
  bool done=false;

  Serial.printf("Attempting open of /jumpman.xfd - Attempt #%d\n\n",tnfsPacket.retryCount);
  tnfsPacket.command=0x29; // OPEN command
  tnfsPacket.data[0]=0x00; // Read only
  tnfsPacket.data[1]=0x00; // "     "
  tnfsPacket.data[2]=0x00; // Mode 000
  tnfsPacket.data[3]=0x00; // "     "
  tnfsPacket.data[4]='/';  // Filename (0 terminated)
  tnfsPacket.data[5]='j';
  tnfsPacket.data[6]='u';
  tnfsPacket.data[7]='m';
  tnfsPacket.data[8]='p';
  tnfsPacket.data[9]='m';
  tnfsPacket.data[10]='a';
  tnfsPacket.data[11]='n';
  tnfsPacket.data[12]='.';
  tnfsPacket.data[13]='x';
  tnfsPacket.data[14]='f';
  tnfsPacket.data[15]='d';
  tnfsPacket.data[16]=0x00;

  // Send command.
  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket.rawData,20);
  UDP.endPacket();

  while(done==false)
  {
    dur=millis()-start;
    if (dur>5000)
    {
      Serial.printf("Timeout. Retrying.\n");
      done=true;
    }
    else if (UDP.parsePacket())
    {
      UDP.read(tnfsPacket.rawData,1024);
      if (tnfsPacket.data[0]==0x00)
      {
        tnfsReadfd=tnfsPacket.data[1];
        Serial.printf("Open successful, file handle %x\n",tnfsReadfd);
        done=true;
        tnfs_state=READ;
      }
      else
      {
        Serial.printf("Open error: 0x%02x",tnfsPacket.data[0]);
        done=true;
        tnfs_state=DONE;
      }
    }
  }
}

/**
 * Read
 */
void tnfs_read()
{

}

/**
 * Close
 */
void tnfs_close()
{

}

/**
 * umount
 */
void tnfs_umount()
{

}

/**
 * The main state machine.
 */
void loop() {

  switch (tnfs_state)
  {
    case MOUNT:
      tnfs_mount();
      break;
    case OPEN:
      tnfs_open();
      break;
    case READ:
      tnfs_read();
      break;
    case CLOSE:
      tnfs_close();
      break;
    case UMOUNT:
      tnfs_umount();
      break;
    case DONE:
      break;
  }

}
