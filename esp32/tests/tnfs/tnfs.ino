/**
   Test #6 - See if we can make an ESP8266 talk TNFS!
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define TNFS_SERVER "192.168.1.7"
#define TNFS_PORT 16384

enum {MOUNT, OPEN, READ, CLOSE, UMOUNT, DONE} tnfs_state=MOUNT;

WiFiUDP UDP;


byte tnfsPacket[512];
int tnfsPacketLen = 0;
byte tnfsReadfd=0;
byte tnfsRetryCount=0;
byte session_idl;
byte session_idh;
int start;
int dur;
int totalLen=0;
int sector;

/**
   Setup.
*/
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("#AtariWiFi Test #6: TNFS client");
  Serial.print("Connecting to WiFi...");
  WiFi.begin("SSID", "PASSWORD");

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
  memset(tnfsPacket,0,sizeof(tnfsPacket));
  tnfsPacket[0]=0x00;       // Session ID
  tnfsPacket[1]=0x00;       // "   "
  tnfsPacket[2]=tnfsRetryCount++; // Retry Count
  tnfsPacket[3]=0x00;       // Command
  tnfsPacket[4]=0x01;       // vers
  tnfsPacket[5]=0x00;       // "  "
  tnfsPacket[6]=0x00;       // Flags
  tnfsPacket[7]=0x00;       // "  "
  tnfsPacket[8]=0x2F;       // '/'
  tnfsPacket[9]=0x00;       // NUL terminated

  Serial.printf("Mounting / from %s, Attempt #%d\n\n",TNFS_SERVER,tnfsRetryCount);

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket,10);
  UDP.endPacket();

  start=millis();
  dur=millis()-start;

  while (dur < 5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket,sizeof(tnfsPacket));
      if (tnfsPacket[4]==0)
      {
        // Successful
        Serial.printf("Successful. Version %d.%d - Session ID: 0x%02x%02x, Suggested Timeout: %d ms.\n",tnfsPacket[6],tnfsPacket[5],tnfsPacket[1],tnfsPacket[0],tnfsPacket[7]+256*tnfsPacket[8]);
        session_idl=tnfsPacket[0];
        session_idh=tnfsPacket[1];
        tnfs_state=OPEN;
        return;
      }
      else
      {
        // Error
        Serial.printf("Error #%02x\n",tnfsPacket[4]);
        return;
      }
    }
  }
  Serial.printf("Request timed out. Retrying...\n\n"); 
}

/**
 * Open
 */
void tnfs_open()
{
  int i=0;
  memset(tnfsPacket,0,sizeof(tnfsPacket));
  tnfsPacket[i++]=session_idl;       // Session ID
  tnfsPacket[i++]=session_idh;       // "   "
  tnfsPacket[i++]=tnfsRetryCount++; // Retry Count
  tnfsPacket[i++]=0x29;       // Command (open)
  tnfsPacket[i++]=0x01;       // Mode 1 (R/O)
  tnfsPacket[i++]=0x00;       // "  "
  tnfsPacket[i++]=0x01;       // Flags
  tnfsPacket[i++]=0x00;       // "  "
  tnfsPacket[i++]=0x2F;       // '/'
  tnfsPacket[i++]='j';        // Filename
  tnfsPacket[i++]='u';        //
  tnfsPacket[i++]='m';        //
  tnfsPacket[i++]='p';        //
  tnfsPacket[i++]='m';        //
  tnfsPacket[i++]='a';        //
  tnfsPacket[i++]='n';        //
  tnfsPacket[i++]='.';        //
  tnfsPacket[i++]='x';        //
  tnfsPacket[i++]='f';        //
  tnfsPacket[i++]='d';        //
  tnfsPacket[i++]=0x00;       // NUL terminated
  tnfsPacket[i++]=0x00;       // No username
  tnfsPacket[i++]=0x00;       // No password
  
  Serial.printf("Opening '/jumpman.xfd' read-only, Attempt #%d\n\n",tnfsRetryCount);

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket,i);
  UDP.endPacket();

  start=millis();
  dur=millis()-start;

  while (dur < 5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket,sizeof(tnfsPacket));
      if (tnfsPacket[4]==0)
      {
        // Successful
        Serial.printf("Open successful, FD #%02x",tnfsPacket[5]);
        tnfsReadfd=tnfsPacket[5];
        tnfs_state=READ;
        sector=0;
        return;
      }
      else
      {
        // Error
        Serial.printf("Error #%02x\n",tnfsPacket[4]);
        return;
      }
    }
  }
  Serial.printf("Request timed out. Retrying...\n\n"); 
}


/**
 * Open
 */
void tnfs_read()
{
  int i=0;
  memset(tnfsPacket,0,sizeof(tnfsPacket));
  tnfsPacket[i++]=session_idl;       // Session ID
  tnfsPacket[i++]=session_idh;       // "   "
  tnfsPacket[i++]=tnfsRetryCount++; // Retry Count
  tnfsPacket[i++]=0x21;       // read
  tnfsPacket[i++]=tnfsReadfd;       // fd
  tnfsPacket[i++]=0x80;       // 128 bytes
  tnfsPacket[i++]=0x00;       // "   "
  
  Serial.printf("Reading next sector...\n\n");

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  UDP.write(tnfsPacket,i);
  UDP.endPacket();

  start=millis();
  dur=millis()-start;

  while (dur < 5000)
  {
    if (UDP.parsePacket())
    {
      int l=UDP.read(tnfsPacket,sizeof(tnfsPacket));
      if (tnfsPacket[4]==0)
      {
        // Successful
        Serial.printf("Sector #%d\n",sector++);
        for (i=7;i<135;i++)
        {
          Serial.printf("%02x ",tnfsPacket[i]);
          if (i%16==0)
            Serial.printf("\n");
        }
          
        if (totalLen>92160)
          tnfs_state=CLOSE;
        else
          totalLen+=128;
        return;
      }
      else
      {
        // Error
        Serial.printf("Error #%02x\n",tnfsPacket[4]);
        tnfs_state=CLOSE;
        return;
      }
    }
  }
  Serial.printf("Request timed out. Retrying...\n\n"); 
}

/**
   The main state machine.
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
      break;
    case UMOUNT:
      break;
    case DONE:
      break;
  }
}
