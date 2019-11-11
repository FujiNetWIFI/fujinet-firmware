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

enum {MOUNT_REQU, MOUNT_RESP,
      OPEN_REQU, OPEN_RESP,
      READ_REQU, READ_RESP,
      CLOSE_REQU, CLOSE_RESP,
      UMOUNT_REQU, UMOUNT_RESP,
      DONE} tnfs_state = MOUNT_REQU;

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

int tnfsPacketLen = 0;
byte tnfsReadfd;
unsigned short session_id;

int start;
int dur;

/**
   Setup.
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
   Ask server to mount /
*/
void tnfs_mount_requ()
{
  Serial.printf("Sending mount request for / to %s attempt #%d\n", TNFS_SERVER, ++tnfsPacket.retryCount);

  // Construct mount packet
  memset(tnfsPacket.rawData,0,1024);
  tnfsPacket.command = 0x00;  // Mount
  tnfsPacket.data[0] = 0x00; // request 1.0
  tnfsPacket.data[1] = 0x01; // "   "
  tnfsPacket.data[2] = '/'; // " / "
  tnfsPacket.data[3] = 0x00; // nul terminated
  tnfsPacket.data[4] = 0x00; // No username
  tnfsPacket.data[5] = 0x00; // No password

  for (int i=0;i<6+4;i++)
    Serial.printf("%02x ",tnfsPacket.rawData[i]);

  Serial.printf("\n\n");

  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);
  UDP.write(tnfsPacket.rawData, 6+4);
  UDP.endPacket();

  // And wait for response.
  tnfs_state = MOUNT_RESP;
  dur = 0;
  start = millis();
}

/**
   Wait for response to mount /
*/
void tnfs_mount_resp()
{ 
  while (dur < 5000)
  {
    dur = millis() - start;
    if (UDP.parsePacket())
    {
      int len = UDP.read(tnfsPacket.rawData, 1024);
      for (int i=0;i<len;i++)
        Serial.printf("%02x ",tnfsPacket.rawData[i]);

      Serial.printf("\n\n");
      
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
        session_id=tnfsPacket.session_id;
        Serial.printf("/ mounted successfully, session id: %x\n", session_id);
        tnfsPacket.retryCount = 0;
        tnfs_state = OPEN_REQU;
        return;
      }
      else
      {
        Serial.printf("/ mount error #%02x", tnfsPacket.data[0]);
        tnfs_state = MOUNT_REQU;
        return;
      }
    }
  }
  Serial.printf("mount request timed out (5000ms)\n Retrying.\n");
  tnfs_state=MOUNT_REQU;
  start=millis();
  dur=0;
}

/**
 * Open Request
 */
void tnfs_open_requ()
{
  Serial.printf("Opening 'jumpman.xfd', attempt #%d\n",tnfsPacket.retryCount);
  memset(tnfsPacket.rawData,0,1024);
  tnfsPacket.command=0x29; // 0x29 = open
  tnfsPacket.session_id=session_id;
  tnfsPacket.data[0]=0x01; // 1 = open read only
  tnfsPacket.data[1]=0x00; // "    "
  tnfsPacket.data[2]=0x00; // 0 = chmod 000
  tnfsPacket.data[3]=0x00; // "    "
  tnfsPacket.data[4]='/';  // Filename
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

  for (int i=0;i<17+4;i++)
  {
    Serial.printf("%02x ",tnfsPacket.rawData[i]);  
  }

  Serial.printf("\n\n");

  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);
  UDP.write(tnfsPacket.rawData, 17+4);
  UDP.endPacket();

  // and wait for response
  tnfs_state = OPEN_RESP;
  dur=0;
  start=millis();
}

/**
 * Open Response
 */
void tnfs_open_resp()
{
  dur = millis() - start;
  while (dur < 5000)
  {
    if (UDP.parsePacket())
    {
      int len=UDP.read(tnfsPacket.rawData, 1024);
      for (int i=0;i<len;i++)
      {
        Serial.printf("%02x ",tnfsPacket.rawData[i]);
      }
      Serial.printf("\n\n");
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
        tnfsReadfd=tnfsPacket.data[1];
        Serial.printf("/jumpman.xfd opened successfully, fd: %x\n", tnfsReadfd);
        tnfsPacket.retryCount = 0;
        tnfs_state = READ_REQU;
        return;
      }
      else
      {
        Serial.printf("/jumpman.xfd open error #%02x\n", tnfsPacket.data[0]);
        tnfs_state = DONE;
        return;
      }
    }
  }
  Serial.printf("open request timed out (5000ms)\n Retrying.");
  tnfs_state=OPEN_REQU;
  tnfsPacket.retryCount=0;
}

/**
 * Read Request
 */
void tnfs_read_requ()
{
  
}

/**
 * Read Response
 */
void tnfs_read_resp()
{

}

/**
 * Close Request
 */
void tnfs_close_requ()
{

}

/**
 * Close Response
 */
void tnfs_close_resp()
{

}

/**
 * Umount request
 */
void tnfs_umount_requ()
{
  
}

/**
 * Umount response
 */
void tnfs_umount_resp()
{
  
}

/**
   The main state machine.
*/
void loop() {

  switch (tnfs_state)
  {
    case MOUNT_REQU:
      tnfs_mount_requ();
      break;
    case MOUNT_RESP:
      tnfs_mount_resp();
      break;
    case OPEN_REQU:
      tnfs_open_requ();
      break;
    case OPEN_RESP:
      tnfs_open_resp();
      break;
    case READ_REQU:
      tnfs_read_requ();
      break;
    case READ_RESP:
      tnfs_read_resp();
      break;
    case CLOSE_REQU:
      tnfs_close_requ();
      break;
    case CLOSE_RESP:
      tnfs_close_resp();
      break;
    case UMOUNT_REQU:
      tnfs_umount_requ();
      break;
    case UMOUNT_RESP:
      tnfs_umount_resp();
    case DONE:
      break;
  }

}
