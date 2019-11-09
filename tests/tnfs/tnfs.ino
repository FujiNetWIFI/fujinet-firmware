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

byte buf[256];

enum {MOUNT, OPEN, READ, CLOSE, UMOUNT, DONE} tnfs_state = MOUNT;

const char* ssid = "Cherryhomes";
const char* password = "e1x64XC46";

byte retryCount = 0;
int file_len;
unsigned short session_id;
byte session_id0;
byte session_id1;
byte read_fd;

#define TNFS_SERVER "192.168.1.7"
#define TNFS_PORT 16384

WiFiUDP UDP;

/**
   Test #6 - See if we can make an ESP8266 talk TNFS!
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
   mount TNFS server
*/
void tnfs_mount()
{
  int len;
  int start = millis();
  int dur = millis() - start;
  bool done=false;

  Serial.printf("Mounting / on %s - Attempt #%d\n\n", TNFS_SERVER, retryCount + 1);
  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);

  // ID, none yet as we are mounting.
  UDP.write("\x00\x00");

  // Retry count.
  UDP.write(retryCount);

  // Mount command.
  UDP.write("\x00\x02\x01/\x00\x00\x00");
  UDP.endPacket();
  retryCount = 0;

  while (done == false)
  {
    dur = millis() - start;
    if (dur > 3000)
    {
      // Timeout.
      Serial.println("Timeout, retrying.");
      retryCount++;
      done = true;
    }
    else if (UDP.parsePacket() > 0)
    {
      len = UDP.read(buf, 9);
      if (buf[4] == 0x00)
      {
        // Successful, get session ID
        session_id = buf[1] + 256 * buf[0];
        session_id0 = buf[0];
        session_id1 = buf[1];
        Serial.println("Mount successful.");
        Serial.printf("Session ID: 0x%04x\n", session_id);
        Serial.printf("Server Version: %d.%d\n", buf[6], buf[5]);
        done = true;
        tnfs_state = READ;
        retryCount = 0;
      }
      else
      {
        Serial.printf("ERROR 0x%02x, retrying.",buf[4]);
        done=true;
        retryCount++;  
      }
    }
  }
}

/**
 * Open jumpman.xfd
 */
void tnfs_open()
{
  int len;
  int start=millis();
  int dur=millis()-start;
  bool done=false;

  Serial.printf("Opening jumpman.xfd...");
  
  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  buf[0]=session_id0;
  buf[1]=session_id1;
  buf[2]=retryCount;
  buf[3]=0x29;
  buf[4]=0x01;
  buf[5]=0x00;
  buf[6]=0x00;
  buf[7]=0x00;
  buf[8]='/';
  buf[9]='j';
  buf[10]='u';
  buf[11]='m';
  buf[12]='p';
  buf[13]='m';
  buf[14]='a';
  buf[15]='n';
  buf[16]='.';
  buf[17]='x';
  buf[18]='f';
  buf[19]='d';
  buf[20]=0x00;
  UDP.write(buf,21);
  UDP.endPacket();

  while (done==false)
  {
    dur=millis()-start;
    if (dur>3000)
    {
      // timeout
      Serial.println("Timeout, retrying.");
      retryCount++;
      done=true;  
    }
    else if (UDP.parsePacket() > 0)
    {
      len=UDP.read(buf,64);
      if (buf[4]==0x00)
      {
        read_fd=buf[5];
        Serial.printf("File opened, fd=%d\n",read_fd); 
        done=true;
        retryCount=0;
        tnfs_state=READ;
      }  
    }  
  }
}

/**
   Attempt to read next 128 bytes
*/
void tnfs_read()
{
  int len;
  int start = millis();
  int dur = millis() - start;
  int recvlen;
  bool done=false;
  buf[0] = session_id0;
  buf[1] = session_id1;
  buf[2] = retryCount;
  buf[3] = 0x21; // READ
  buf[4] = read_fd; // the file descriptor returned.
  buf[5] = 0x80; // 128 bytes
  buf[6] = 0x00;
  Serial.printf("--- Reading next sector\n");
  UDP.beginPacket(TNFS_SERVER, TNFS_PORT);
  UDP.write(buf,7);
  UDP.endPacket();

  while(done==false)
  {
    dur=millis()-start;
    if (dur>3000)
    {
      // timeout
      Serial.println("Timeout, retrying.");
      retryCount++;
      done=true;
    }
    else if (UDP.parsePacket() > 0)
    {
      len = UDP.read(buf,256);
      if (buf[4]=0x00)
      {
        recvlen=buf[6]*256+buf[5];
        for (int i=0;i<recvlen;i+=16)
        {
          printf("%x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02 %x02\n",
          buf[i+7+0],buf[i+7+1],buf[i+7+2],buf[i+7+3],buf[i+7+4],buf[i+7+5],buf[i+7+6],buf[i+7+7],buf[i+7+8],buf[i+7+9],buf[i+7+10],buf[i+7+11],buf[i+7+12],buf[i+7+13],buf[i+7+14],buf[i+7+15]);
        }
        done=true;
        retryCount=0;
        tnfs_state=CLOSE;
      }  
    }
  }
}

/**
 * TNFS close
 */
void tnfs_close()
{
  int len;
  int start=millis();
  int dur=millis()-start;
  bool done=false;

  Serial.printf("Closing FD: %d\n",read_fd);

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  buf[0]=session_id0;
  buf[1]=session_id1;
  buf[2]=retryCount;
  buf[3]=0x23;
  buf[4]=read_fd;
  UDP.write(buf,5);
  UDP.endPacket();

  tnfs_state=UMOUNT;
}

/**
 * TNFS umount
 */
void tnfs_umount()
{
  Serial.printf("Umounting server.\n");

  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  buf[0]=session_id0;
  buf[1]=session_id1;
  buf[2]=retryCount;
  buf[3]=0x01;
  UDP.write(buf,4);

  tnfs_state=DONE;
}

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
