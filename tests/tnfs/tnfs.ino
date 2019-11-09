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

union
{
  struct
  {
    unsigned short session_id;
    unsigned char retryCount;
    unsigned char command;
  };
  unsigned char rawData[4];  
} tnfsHdr;

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

  // Prime for mount command.
  tnfsHdr.session_id=0;
  tnfsHdr.retryCount=0;
  tnfsHdr.command=0;
}

/**
 * Write the header, beginPacket should have already been called.
 */
void tnfs_write_header()
{
  UDP.write(tnfsHdr.rawData,4);
}

/**
 * Read the header, parsePacket should have already been called.
 */
void tnfs_read_header()
{
  UDP.read(tnfsHdr.rawData,4);  
}

/**
 * Write the packet
 */
void tnfs_write_packet(unsigned char len)
{
  UDP.beginPacket(TNFS_SERVER,TNFS_PORT);
  tnfs_write_header();
  UDP.write(buf,len);
  UDP.endPacket();
}

/**
 * Read TNFS payload, returns length.
 */
int tnfs_read_packet()
{
  tnfs_read_header();
  UDP.read(buf,sizeof(buf));
}

/**
 * Mount server
 */
void tnfs_mount()
{
  int start=millis();
  int dur=millis()-start;
  int len=0;
  bool done=false;

  Serial.printf("Attempting to mount: %s retry #%d\n",TNFS_SERVER,tnfsHdr.retryCount);
  
  buf[0]=0x00;  // version 1.0 requested
  buf[1]=0x01;
  buf[2]='/';   // Request / path
  buf[3]=0x00;  // terminate path string
  buf[4]=0x00;  // no username
  buf[5]=0x00;  // no password
  tnfs_write_packet(6); // Write the mount

  while (dur<5000)
  {
    if (UDP.parsePacket()>0)
    {
      len=tnfs_read_packet();
      if (buf[0]==0x00)
      {
        Serial.printf("Mount successful. Session ID: 0x%04x\n",tnfsHdr.session_id);
        tnfs_state=OPEN;
        tnfsHdr.retryCount=0;
        break;
      }
      else
      {
        Serial.printf("Mount error: 0x%02x\n",buf[4]);
        UDP.flush();
        break;
      }
    }
  }
  if (dur>5000)
    Serial.printf("Timed out.");  
}

/**
 * Open test file
 */
void tnfs_open()
{
  int start=millis();
  int dur=millis()-start;
  int len=0;
  bool done=false;

  Serial.printf("Attempting open of jumpman.xfd.\n");

  buf[0]=0x29;  // open
  buf[1]=0x00;  // flags (R)
  buf[2]=0x00;  //
  buf[3]=0x00;  // mode 
  buf[4]=0x00;
  buf[5]='/';
  buf[6]='j';   // filename
  buf[7]='u';   //
  buf[8]='m';   //
  buf[9]='p';   //
  buf[10]='m';   //
  buf[11]='a';  //
  buf[12]='n';  //
  buf[13]='.';  //
  buf[14]='x';  //
  buf[15]='f';  //
  buf[16]='d';  //
  buf[17]=0x00; // end filename
  tnfs_write_packet(18);

  while (dur<5000)
  {
    if (UDP.parsePacket()>0)
    {
      len=tnfs_read_packet();
      
      for (int i=0;i<len;i++)
        Serial.printf("%02x ",buf[i]);
      
      if (buf[0]==0x00)
      {
        Serial.printf("open successful. file descriptor: 0x%04x\n",buf[1]);
        tnfs_state=READ;
        tnfsHdr.retryCount=0;
        break;
      }
      else
      {
        Serial.printf("open error: 0x%02x\n",buf[0]);
        break;
      }
    }
  }
  if (dur>5000)
    Serial.printf("Timed out.");    
}

/**
 * Attempt reads
 */
void tnfs_read()
{
}

/**
 * Close file
 */
void tnfs_close()
{
}

/**
 * Unmount server
 */
void tnfs_umount()
{
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
