/**
   Test #19 - Config Program, try 2
*/

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <FS.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#include <SPIFFS.h>
#endif

#include <WiFiUdp.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug UART
//#define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
//#define DEBUG_N
//#define DEBUG_HOST "192.168.1.117"
//#define DEBUG_SSID "YourSSID"
//#define DEBUG_PASSWORD "YourWiFiPassword"

#ifdef ESP8266
#define SIO_UART Serial
#define BUG_UART Serial1
#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12
#endif
#ifdef ESP32
#define SIO_UART Serial2
#define BUG_UART Serial
#define PIN_LED1         2
#define PIN_LED2         4
#define PIN_INT         26
#define PIN_PROC        22
#define PIN_MTR         33
#define PIN_CMD         21
#endif

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

WiFiUDP UDP;
File atr;

unsigned long cmdTimer = 0;

/**
   A Single command frame, both in structured and unstructured
   form.
*/
union
{
  struct
  {
    unsigned char devic;
    unsigned char comnd;
    unsigned char aux1;
    unsigned char aux2;
    unsigned char cksum;
  };
  byte cmdFrameData[5];
} cmdFrame;

/**
   A single SSID entry
*/
union
{
  struct
  {
    char ssid[32];
    char rssi;
  };
  unsigned char rawData[33];
} ssidInfo;

/**
   Network Configuration
*/
union
{
  struct
  {
    char ssid[32];
    char password[64];
  };
  unsigned char rawData[96];
} netConfig;

/**
   TNFS Packet
*/
union
{
  struct
  {
    byte session_idl;
    byte session_idh;
    byte retryCount;
    byte command;
    byte data[508];
  };
  byte rawData[512];
} tnfsPacket;

byte sector[128];
char tnfsServer[256];
char mountPath[256];
char current_entry[256];
char tnfs_fd = 0xFF; // boot internal by default.
char tnfs_dir_fd;

#ifdef DEBUG_N
WiFiClient wificlient;
#endif

char packet[256];

#ifdef DEBUG_S
#define Debug_print(...) BUG_UART.print( __VA_ARGS__ )
#define Debug_printf(...) BUG_UART.printf( __VA_ARGS__ )
#define Debug_println(...) BUG_UART.println( __VA_ARGS__ )
#define DEBUG
#endif
#ifdef DEBUG_N
#define Debug_print(...) wificlient.print( __VA_ARGS__ )
#define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
#define Debug_println(...) wificlient.println( __VA_ARGS__ )
#define DEBUG
#endif

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
   ISR for falling COMMAND
*/
void ICACHE_RAM_ATTR sio_isr_cmd()
{
  if (digitalRead(PIN_CMD) == LOW)
  {
    cmdState = ID;
    cmdTimer = millis();
  }
}

/**
   Get ID
*/
void sio_get_id()
{
  cmdFrame.devic = SIO_UART.read();
  if (cmdFrame.devic == 0x31 || cmdFrame.devic == 0x70)
    cmdState = COMMAND;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG
  Debug_print("CMD DEVC: ");
  Debug_println(cmdFrame.devic, HEX);
#endif
}

void sio_get_command()
{
  cmdFrame.comnd = SIO_UART.read();
  cmdState = AUX1;

#ifdef DEBUG
  Debug_print("CMD CMND: ");
  Debug_println(cmdFrame.comnd, HEX);
#endif
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  cmdFrame.aux1 = SIO_UART.read();
  cmdState = AUX2;

#ifdef DEBUG
  Debug_print("CMD AUX1: ");
  Debug_println(cmdFrame.aux1, HEX);
#endif
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  cmdFrame.aux2 = SIO_UART.read();
  cmdState = CHECKSUM;

#ifdef DEBUG
  Debug_print("CMD AUX2: ");
  Debug_println(cmdFrame.aux2, HEX);
#endif
}

/**
   Get Checksum, and compare
*/
void sio_get_checksum()
{
  byte ck;
  cmdFrame.cksum = SIO_UART.read();
  ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

#ifdef DEBUG
  Debug_print("CMD CKSM: ");
  Debug_print(cmdFrame.cksum, HEX);
#endif

  if (ck == cmdFrame.cksum)
  {
#ifdef DEBUG
    Debug_println(", ACK");
#endif
    sio_ack();
  }
  else
  {
#ifdef DEBUG
    Debug_println(", NAK");
#endif
    sio_nak();
  }
}

/**
   scan for networks
*/
void sio_scan_networks()
{
  byte ck;
  char totalSSIDs;
  char ret[4]={0,0,0,0};
  
  WiFi.mode(WIFI_STA);
  totalSSIDs = WiFi.scanNetworks();
  ret[0]=totalSSIDs;

#ifdef DEBUG
  Debug_printf("Scan networks returned: %d\n\n",totalSSIDs);
#endif
  ck = sio_checksum((byte *)&ret, 4);

  SIO_UART.write('C');     // Completed command
  SIO_UART.flush();

  delayMicroseconds(DELAY_T5); // t5 delay

  // Write data frame
  SIO_UART.write((byte *)&ret, 4);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();

#ifdef DEBUG
  Debug_printf("Wrote data packet/Checksum: $%02x $%02x $%02x $%02x/$02x\n\n",ret[0],ret[1],ret[2],ret[3],ck);
#endif
  delayMicroseconds(200);
}

/**
   Return scanned network entry
*/
void sio_scan_result()
{
  byte ck;

  strcpy(ssidInfo.ssid, WiFi.SSID(cmdFrame.aux1).c_str());
  ssidInfo.rssi = (char)WiFi.RSSI(cmdFrame.aux1);

  ck = sio_checksum((byte *)&ssidInfo.rawData, 33);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');     // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(ssidInfo.rawData, 33);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   SIO set SSID/Password
*/
void sio_set_ssid()
{
  byte ck;

  SIO_UART.readBytes(netConfig.rawData, 96);
  ck = SIO_UART.read(); // Read checksum
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(netConfig.rawData, 96))
  {
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
    WiFi.begin(netConfig.ssid, netConfig.password);
    UDP.begin(16384);
    yield();
  }
}

/**
   SIO get WiFi Status
*/
void sio_get_wifi_status()
{
  byte ck;
  char wifiStatus;

  wifiStatus = WiFi.status();

  if (wifiStatus == WL_CONNECTED)
  {
#ifdef ESP8266
    digitalWrite(PIN_LED, LOW); // turn on LED
  }
  else
  {
    digitalWrite(PIN_LED, HIGH); // turn off LED
  }
#elif defined(ESP32)
    digitalWrite(PIN_LED1, LOW); // turn on LED
  }
  else
  {
    digitalWrite(PIN_LED1, HIGH); // turn off LED
  }
#endif
  
  ck = sio_checksum((byte *)&wifiStatus, 1);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');     // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(wifiStatus);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   Write, called for both W and P commands.
*/
void sio_write()
{
  byte ck;
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16; // skip 16 byte ATR Header

#ifdef DEBUG_S
  Debug_printf("receiving 128b data frame from computer.\n");
#endif

  SIO_UART.readBytes(sector, 128);
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);

  if (ck == sio_checksum(sector, 128))
  {
    SIO_UART.write('A'); // Write ACK
    delayMicroseconds(DELAY_T5);

    if (tnfs_fd == 0xFF)
    {
      atr.seek(offset, SeekSet);
      atr.write(sector, 128);
      atr.flush();
    }
    else
    {
      tnfs_seek(offset);
      tnfs_write();
    }
    SIO_UART.write('C');
    yield();
  }
  else
    SIO_UART.write('N'); // Write NAK
}

/**
   format (fake)
*/
void sio_format()
{
  byte ck;

  for (int i = 0; i < 128; i++)
    sector[i] = 0;

  sector[0] = 0xFF; // no bad sectors.
  sector[1] = 0xFF;

  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector, 128);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG_S
  Debug_printf("We faked a format.\n");
#endif
}

/**
   SIO TNFS server mount
*/
void sio_mount_host()
{
  byte ck;

  SIO_UART.readBytes(tnfsServer, 256);
  ck = SIO_UART.read(); // Read checksum
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum((byte *)&tnfsServer, 256))
  {
    delayMicroseconds(DELAY_T5);
    tnfs_mount();
    SIO_UART.write('C');
    yield();
  }
}

/**
   SIO Mount
*/
void sio_mount_image()
{
  byte ck;

  SIO_UART.readBytes(mountPath, 256);
  ck = SIO_UART.read(); // Read checksum
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum((byte *)&mountPath, 256))
  {
    delayMicroseconds(DELAY_T5);
    tnfs_open();
    SIO_UART.write('C');
    yield();
  }
}

/**
   SIO Open TNFS Directory
*/
void sio_open_tnfs_directory()
{
  byte ck;
#ifdef DEBUG
  Debug_println("Receiving 256b frame from computer");
#endif

  SIO_UART.readBytes(current_entry, 256);
  ck = SIO_UART.read(); // Read checksum

  if (ck != sio_checksum((byte *)&current_entry, 256))
  {
    SIO_UART.write('N'); // NAK
    return;
  }

  SIO_UART.write('A');   // ACK

  tnfs_opendir((256 * cmdFrame.aux2) + cmdFrame.aux1);

  // And complete.
  SIO_UART.write('C');
}

/**
   Read TNFS directory (next entry)
*/
void sio_read_tnfs_directory()
{
  byte ck;
  long offset;
  byte ret;

  memset(current_entry, 0x00, 256);

  ret = tnfs_readdir();

  if (ret == false)
  {
    current_entry[0] = 0x7F; // end of dir
  }

  ck = sio_checksum((byte *)&current_entry, cmdFrame.aux1);

  delayMicroseconds(DELAY_T5); // t5 delay

  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();

  delayMicroseconds(200);

  // Write data frame
  SIO_UART.write((byte *)current_entry, cmdFrame.aux1);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   SIO close TNFS Directory
*/
void sio_close_tnfs_directory()
{
  delayMicroseconds(DELAY_T5);
  tnfs_closedir();
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();
}

/**
   Process command
*/

void sio_process()
{
  switch (cmdFrame.comnd)
  {
    case 'P':
    case 'W':
      sio_write();
      break;
    case 'R':
      sio_read();
      break;
    case 'S':
      sio_status();
      break;
    case '!':
      sio_format();
      break;
    case 0xFD:
      sio_scan_networks();
      break;
    case 0xFC:
      sio_scan_result();
      break;
    case 0xFB:
      sio_set_ssid();
      break;
    case 0xFA:
      sio_get_wifi_status();
      break;
    case 0xF9:
      sio_mount_host();
      break;
    case 0xF8:
      sio_mount_image();
      break;
    case 0xF7:
      sio_open_tnfs_directory();
      break;
    case 0xF6:
      sio_read_tnfs_directory();
      break;
    case 0xF5:
      sio_close_tnfs_directory();
      break;
  }

  cmdState = WAIT;
  cmdTimer = 0;
}

/**
   Read
*/
void sio_read()
{
  byte ck;
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16; // skip 16 byte ATR Header

  if (tnfs_fd == 0xFF) // no TNFS ATR mounted.
  {
    atr.seek(offset, SeekSet);
    atr.read(sector, 128);
  }
  else // TNFS ATR mounted and opened...
  {
    tnfs_seek(offset);
    tnfs_read();
    for (int i = 0; i < 128; i++)
      sector[i] = tnfsPacket.data[i + 3];
  }

  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector, 128);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG
  Debug_print("SIO READ OFFSET: ");
  Debug_print(offset);
  Debug_print(" - ");
  Debug_println((offset + 128));
#endif
}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
  byte ck;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  SIO_UART.write('A');
  SIO_UART.flush();
  //cmdState = PROCESS;
  sio_process();
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  cmdState = WAIT;
  cmdTimer = 0;
}

void sio_incoming() {
  switch (cmdState)
  {
    case ID:
      sio_get_id();
      break;
    case COMMAND:
      sio_get_command();
      break;
    case AUX1:
      sio_get_aux1();
      break;
    case AUX2:
      sio_get_aux2();
      break;
    case CHECKSUM:
      sio_get_checksum();
      break;
    case ACK:
      sio_ack();
      break;
    case NAK:
      sio_nak();
      break;
    case PROCESS:
      sio_process();
      break;
    case WAIT:
      SIO_UART.read(); // Toss it for now
      cmdTimer = 0;
      break;
  }
}

/**
   Mount the TNFS server
*/
void tnfs_mount()
{
  int start = millis();
  int dur = millis() - start;

  memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));
  tnfsPacket.session_idl = 0;
  tnfsPacket.session_idh = 0;
  tnfsPacket.retryCount = 0;
  tnfsPacket.command = 0;
  tnfsPacket.data[0] = 0x01; // vers
  tnfsPacket.data[1] = 0x00; // "  "
  tnfsPacket.data[2] = 0x2F; // /
  tnfsPacket.data[3] = 0x00; // nul
  tnfsPacket.data[4] = 0x00; // no username
  tnfsPacket.data[5] = 0x00; // no password

#ifdef DEBUG
  Debug_print("Mounting / from ");
  Debug_println((char*)tnfsServer);
  Debug_print("Req Packet: ");
  for (int i = 0; i < 10; i++)
  {
    Debug_print(tnfsPacket.rawData[i], HEX);
    Debug_print(" ");
  }
  Debug_println("");
#endif /* DEBUG_S */

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 10);
  UDP.endPacket();

#ifdef DEBUG
  Debug_println("Wrote the packet");
#endif

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
#ifdef DEBUG
      Debug_print("Resp Packet: ");
      for (int i = 0; i < l; i++)
      {
        Debug_print(tnfsPacket.rawData[i], HEX);
        Debug_print(" ");
      }
      Debug_println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
#ifdef DEBUG
        Debug_print("Successful, Session ID: ");
        Debug_print(tnfsPacket.session_idl, HEX);
        Debug_println(tnfsPacket.session_idh, HEX);
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error
#ifdef DEBUG
        Debug_print("Error #");
        Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
        return;
      }
    }
  }
  // Otherwise we timed out.
#ifdef DEBUG
  Debug_println("Timeout after 5000ms");
#endif /* DEBUG_S */
}

/**
   Open 'autorun.atr'
*/
void tnfs_open()
{
  int start = millis();
  int dur = millis() - start;
  int c = 0;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command = 0x29; // OPEN
  tnfsPacket.data[c++] = 0x03; // R/W
  tnfsPacket.data[c++] = 0x00; //
  tnfsPacket.data[c++] = 0x00; // Flags
  tnfsPacket.data[c++] = 0x00; //
  tnfsPacket.data[c++] = '/'; // Filename start

  for (int i = 0; i < strlen(mountPath); i++)
  {
    tnfsPacket.data[i + 5] = mountPath[i];
    c++;
  }

  tnfsPacket.data[c++] = 0x00;
  tnfsPacket.data[c++] = 0x00;
  tnfsPacket.data[c++] = 0x00;

#ifdef DEBUG
  Debug_printf("Opening /%s\n", mountPath);
  Debug_println("");
  Debug_print("Req Packet: ");
  for (int i = 0; i < c + 4; i++)
  {
    Debug_print(tnfsPacket.rawData[i], HEX);
    Debug_print(" ");
  }
#endif /* DEBUG_S */

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, c + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
#ifdef DEBUG
      Debug_print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        Debug_print(tnfsPacket.rawData[i], HEX);
        Debug_print(" ");
      }
      Debug_println("");
#endif DEBUG_S
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
        tnfs_fd = tnfsPacket.data[1];
#ifdef DEBUG
        Debug_print("Successful, file descriptor: #");
        Debug_println(tnfs_fd, HEX);
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // unsuccessful
#ifdef DEBUG
        Debug_print("Error code #");
        Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
   TNFS Open Directory
*/
void tnfs_opendir(unsigned short diroffset)
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command = 0x10; // OPENDIR
  tnfsPacket.data[0] = '/'; // Open root dir
  tnfsPacket.data[1] = 0x00; // nul terminated

#ifdef DEBUG
  Debug_println("TNFS Open directory /");
#endif

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 2 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
        tnfs_dir_fd = tnfsPacket.data[1];
        for (int o = 0; o < diroffset; o++)
          tnfs_readdir();
        return;
      }
      else
      {
        // Unsuccessful
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif
}

/**
   TNFS Read Directory
   Reads the next directory entry
*/
bool tnfs_readdir()
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command = 0x11; // READDIR
  tnfsPacket.data[0] = tnfs_dir_fd; // Open root dir

#ifdef DEBUG
  Debug_println("TNFS Read next dir entry");
#endif

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 1 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
        strcpy((char*)&current_entry, (char *)&tnfsPacket.data[1]);
        return true;
      }
      else
      {
        // Unsuccessful
        return false;
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
   TNFS Close Directory
*/
void tnfs_closedir()
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;  // increase sequence #
  tnfsPacket.command = 0x12; // CLOSEDIR
  tnfsPacket.data[0] = tnfs_dir_fd; // Open root dir

#ifdef DEBUG
  Debug_println("TNFS dir close");
#endif

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 1 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, 512);
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
        return;
      }
      else
      {
        // Unsuccessful
        return;
      }
    }
  }
  // Otherwise, we timed out.
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
   TNFS write
*/
void tnfs_write()
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;  // Increase sequence
  tnfsPacket.command = 0x22; // READ
  tnfsPacket.data[0] = tnfs_fd; // returned file descriptor
  tnfsPacket.data[1] = 0x80; // 128 bytes
  tnfsPacket.data[2] = 0x00; //

#ifdef DEBUG
  Debug_print("Writing to File descriptor: ");
  Debug_println(tnfs_fd);
  Debug_print("Req Packet: ");
  for (int i = 0; i < 7; i++)
  {
    Debug_print(tnfsPacket.rawData[i], HEX);
    Debug_print(" ");
  }
  Debug_println("");
#endif /* DEBUG_S */

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 4 + 3);
  UDP.write(sector, 128);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG
      Debug_print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        Debug_print(tnfsPacket.rawData[i], HEX);
        Debug_print(" ");
      }
      Debug_println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
#ifdef DEBUG
        Debug_println("Successful.");
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error
#ifdef DEBUG
        Debug_print("Error code #");
        Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;
      }
    }
  }
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
   TNFS read
*/
void tnfs_read()
{
  int start = millis();
  int dur = millis() - start;
  tnfsPacket.retryCount++;  // Increase sequence
  tnfsPacket.command = 0x21; // READ
  tnfsPacket.data[0] = tnfs_fd; // returned file descriptor
  tnfsPacket.data[1] = 0x80; // 128 bytes
  tnfsPacket.data[2] = 0x00; //

#ifdef DEBUG
  Debug_print("Reading from File descriptor: ");
  Debug_println(tnfs_fd);
  Debug_print("Req Packet: ");
  for (int i = 0; i < 7; i++)
  {
    Debug_print(tnfsPacket.rawData[i], HEX);
    Debug_print(" ");
  }
  Debug_println("");
#endif /* DEBUG_S */

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 4 + 3);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG
      Debug_print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        Debug_print(tnfsPacket.rawData[i], HEX);
        Debug_print(" ");
      }
      Debug_println("");
#endif /* DEBUG_S */
      if (tnfsPacket.data[0] == 0x00)
      {
        // Successful
#ifdef DEBUG
        Debug_println("Successful.");
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error
#ifdef DEBUG
        Debug_print("Error code #");
        Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;
      }
    }
  }
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

/**
   TNFS seek
*/
void tnfs_seek(long offset)
{
  int start = millis();
  int dur = millis() - start;
  byte offsetVal[4];

  // This may be sending the bytes in the wrong endian, pls check. Easiest way is to flip the indices.
  offsetVal[0] = (int)((offset & 0xFF000000) >> 24 );
  offsetVal[1] = (int)((offset & 0x00FF0000) >> 16 );
  offsetVal[2] = (int)((offset & 0x0000FF00) >> 8 );
  offsetVal[3] = (int)((offset & 0X000000FF));

  tnfsPacket.retryCount++;
  tnfsPacket.command = 0x25; // LSEEK
  tnfsPacket.data[0] = tnfs_fd;
  tnfsPacket.data[1] = 0x00; // SEEK_SET
  tnfsPacket.data[2] = offsetVal[3];
  tnfsPacket.data[3] = offsetVal[2];
  tnfsPacket.data[4] = offsetVal[1];
  tnfsPacket.data[5] = offsetVal[0];

#ifdef DEBUG
  Debug_print("Seek requested to offset: ");
  Debug_println(offset);
  Debug_print("Req packet: ");
  for (int i = 0; i < 10; i++)
  {
    Debug_print(tnfsPacket.rawData[i], HEX);
    Debug_print(" ");
  }
  Debug_println("");
#endif /* DEBUG_S*/

  UDP.beginPacket(tnfsServer, 16384);
  UDP.write(tnfsPacket.rawData, 6 + 4);
  UDP.endPacket();

  while (dur < 5000)
  {
    yield();
    if (UDP.parsePacket())
    {
      int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG
      Debug_print("Resp packet: ");
      for (int i = 0; i < l; i++)
      {
        Debug_print(tnfsPacket.rawData[i], HEX);
        Debug_print(" ");
      }
      Debug_println("");
#endif /* DEBUG_S */

      if (tnfsPacket.data[0] == 0)
      {
        // Success.
#ifdef DEBUG
        Debug_println("Successful.");
#endif /* DEBUG_S */
        return;
      }
      else
      {
        // Error.
#ifdef DEBUG
        Debug_print("Error code #");
        Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
        return;
      }
    }
  }
#ifdef DEBUG
  Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
}

void setup()
{
  SPIFFS.begin();
  atr = SPIFFS.open("/autorun.atr", "r+");
#ifdef DEBUG_S
  BUG_UART.begin(19200);
  Debug_println();
  Debug_println("#FujiNet CIO Test #16 started");
#endif
  // Set up pins
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer
  pinMode(PIN_PROC, OUTPUT); // thanks AtariGeezer
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
#ifdef ESP32
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif
  digitalWrite(PIN_PROC,HIGH);
  digitalWrite(PIN_INT, HIGH);

#ifdef DEBUG_N
  /* Get WiFi started, but don't wait for it otherwise SIO
   * powered FujiNet fails to boot 
   */
  WiFi.begin(DEBUG_SSID, DEBUG_PASSWORD);
#endif

  // Set up serial
  SIO_UART.begin(19200);
#ifdef ESP8266
  SIO_UART.swap();
#endif

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state
}

void loop()
{
#ifdef DEBUG_N
  /* Connect to debug server if we aren't and WiFi is connected */
  if( !wificlient.connected() && WiFi.status() == WL_CONNECTED )
  {
    wificlient.connect(DEBUG_HOST, 6502);
    wificlient.println("#AtariWifi Config Test");
  }
#endif

  if (SIO_UART.available() > 0)
  {
    sio_incoming();
  }

  if (millis() - cmdTimer > CMD_TIMEOUT && cmdState != WAIT)
  {
#ifdef DEBUG
    Debug_print("SIO CMD TIMEOUT: ");
    Debug_println(cmdState);
#endif
    cmdState = WAIT;
    cmdTimer = 0;
  }
}
