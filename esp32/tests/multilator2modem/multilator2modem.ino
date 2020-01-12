
#define TEST_NAME "#FujiNet Multi-Diskulator v2 + Modem850"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#endif

#include <FS.h>
#include <WiFiUdp.h>

// Uncomment for Debug on 2nd UART (GPIO 2)
//#define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
//#define DEBUG_N
//#define DEBUG_HOST "192.168.1.8"


// Uncomment for VERBOSE debug output
//#define DEBUG_VERBOSE

#ifdef ESP8266
#define SIO_UART Serial
#define BUG_UART Serial1
#define PIN_LED2        2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12
#define PIN_CKO         2
#define PIN_CKI        14
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
#define PIN_CKO         32
#define PIN_CKI         27
#endif

#ifdef ESP8266
#define DELAY_T0  750
#define DELAY_T1  650
#define DELAY_T2  0
#define DELAY_T3  1000
#endif
#define DELAY_T4  850
#define DELAY_T5  250

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

struct
{
  unsigned char num_tracks;
  unsigned char step_rate;
  unsigned char sectors_per_trackM;
  unsigned char sectors_per_trackL;
  unsigned char num_sides;
  unsigned char density;
  unsigned char sector_sizeM;
  unsigned char sector_sizeL;
  unsigned char drive_present;
  unsigned char reserved1;
  unsigned char reserved2;
  unsigned char reserved3;
} percomBlock[8];

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
    byte data[512];
  };
  byte rawData[516];
} tnfsPacket;

union
{
  char host[8][32];
  unsigned char rawData[256];
} hostSlots;

union
{
  struct
  {
    unsigned char hostSlot;
    unsigned char mode;
    char file[36];
  } slot[8];
  unsigned char rawData[304];
} deviceSlots;

struct
{
  unsigned char session_idl;
  unsigned char session_idh;
} tnfsSessionIDs[8];

union
{
  struct
  {
    char ssid[32];
    char hostname[64];
    unsigned char localIP[4];
    unsigned char gateway[4];
    unsigned char netmask[4];
    unsigned char dnsIP[4];
    unsigned char macAddress[6];
  };
  unsigned char rawData[118];
} adapterConfig;

#ifdef DEBUG_N
WiFiClient wificlient;
#endif
WiFiUDP UDP;
File atrConfig;
byte sectorCache[8][2560];
byte sector[256];
char tnfsServer[256];
char mountPath[256];
char current_entry[256];
char tnfs_fds[8];
char tnfs_dir_fds[8];
int firstCachedSector[8] = {65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535};
unsigned short sectorSize[8] = {128, 128, 128, 128, 128, 128, 128, 128};
unsigned char max_cached_sectors = 19;
bool load_config = true;
char statusSkip = 0;
void (*cmdPtr[256])(void); // command function pointers
char totalSSIDs;

// Modem 850 Variables
long sioBaud = 19200;         // Baud rate for SIO
long modemBaud = 2400;        // The current BPS setting (default 2400)
bool modemActive = false;     // If modem mode is active
String cmd = "";              // Gather a new AT command to this string from serial
bool cmdMode = true;          // Are we in AT command mode or connected mode
bool telnet = true;           // Is telnet control code handling enabled
long listenPort = 0;          // Listen to this if not connected. Set to zero to disable.
#define RING_INTERVAL 3000    // How often to print RING when having a new incoming connection (ms)
WiFiClient tcpClient;
WiFiServer tcpServer(listenPort);
unsigned long lastRingMs = 0; // Time of last "RING" message (millis())
#define MAX_CMD_LENGTH 256    // Maximum length for AT command
char plusCount = 0;           // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;   // When did we last receive a "+++" sequence
#define LED_TIME 1            // How many ms to keep LED on at activity
unsigned long ledTime = 0;
#define TX_BUF_SIZE 256       // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

// DEBUGGING MACROS /////////////////////////////////////////////////////////////////////////
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
/////////////////////////////////////////////////////////////////////////////////////////////

/**
   Set WiFi LED
*/
void wifi_led(bool onOff)
{
#ifdef ESP32
  digitalWrite(PIN_LED1, (onOff ? LOW : HIGH));
#endif
}

/**
   Set SIO LED
*/
void sio_led(bool onOff)
{
#ifdef ESP32
  digitalWrite(PIN_LED2, (onOff ? LOW : HIGH));
#endif
}

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
   Return true if valid device ID
*/
bool sio_valid_device_id()
{
  unsigned char deviceSlot = cmdFrame.devic - 0x31;
  if ((load_config == true) && (cmdFrame.devic == 0x31)) // Only respond to 0x31 if in config mode
    return true;
  else if (cmdFrame.devic == 0x50) // 850 R: Device Emulator
    return true;
  else if (cmdFrame.devic == 0x70) // respond to FujiNet Network device commands
    return true;
  else if (cmdFrame.devic == 0x4F) // Do not respond to Type 3/4 polls
    return false;
  else if (deviceSlots.slot[deviceSlot].hostSlot != 0xFF) // Only respond to full device slots
    return true;
  else
    return false;
}

/**
   sio NAK
*/
void sio_nak()
{
  SIO_UART.write('N');
#ifdef ESP32
  SIO_UART.flush();
#endif
#ifdef DEBUG
  Debug_println("NAK!");
#endif
}

/**
   sio ACK
*/
void sio_ack()
{
  SIO_UART.write('A');
#ifdef ESP32
  SIO_UART.flush();
#endif
#ifdef DEBUG
  Debug_println("ACK!");
#endif
}

/**
   sio COMPLETE
*/
void sio_complete()
{
  delayMicroseconds(DELAY_T5);
  SIO_UART.write('C');
#ifdef ESP32
  SIO_UART.flush();
#endif
}

/**
   sio ERROR
*/
void sio_error()
{
  delayMicroseconds(DELAY_T5);
  SIO_UART.write('E');
#ifdef ESP32
  SIO_UART.flush();
#endif
}

/**
   sio READ from PERIPHERAL to COMPUTER
   b = buffer to send to Atari
   len = length of buffer
   err = did an error happen before this read?
*/
void sio_to_computer(byte* b, unsigned short len, bool err)
{
  byte ck = sio_checksum(b, len);

#ifdef ESP8266
  //  delayMicroseconds(DELAY_T5);
#endif

  if (err == true)
    sio_error();
  else
    sio_complete();

  //#ifdef ESP32
  delayMicroseconds(DELAY_T5); // not documented, but required
  //#endif

  // Write data frame.
  SIO_UART.write(b, len);

  // Write checksum
  SIO_UART.write(ck);
#ifdef ESP32
  SIO_UART.flush();
#endif

#ifdef DEBUG_VERBOSE
  Debug_printf("TO COMPUTER: ");
  for (int i = 0; i < len; i++)
    Debug_printf("%02x ", b[i]);
  Debug_printf("\r\nCKSUM: %02x\r\n\r\n", ck);
#endif

}

/**
   sio WRITE from COMPUTER to PERIPHERAL
   b = buffer from atari to fujinet
   len = length
   returns checksum reported by atari
*/
byte sio_to_peripheral(byte* b, unsigned short len)
{
  byte ck;

  // Retrieve data frame from computer
  size_t l = SIO_UART.readBytes(b, len);

  // Wait for checksum
  while (!SIO_UART.available())
    yield();

  // Receive Checksum
  ck = SIO_UART.read();

#ifdef DEBUG_VERBOSE
  Debug_printf("l: %d\r\n", l);
  Debug_printf("TO PERIPHERAL: ");
  for (int i = 0; i < len; i++)
    Debug_printf("%02x ", sector[i]);
  Debug_printf("\r\nCKSUM: %02x\r\n\r\n", ck);
#endif

  delayMicroseconds(DELAY_T4);

  if (sio_checksum(b, len) != ck)
  {
    sio_nak();
    return false;
  }
  else
  {
    sio_ack();
  }

  return ck;
}

/**
   Get Adapter config.
*/
void sio_get_adapter_config()
{
  strcpy(adapterConfig.ssid, netConfig.ssid);
#ifdef ESP8266
  strcpy(adapterConfig.hostname, WiFi.hostname().c_str());
#endif
#ifdef ESP32
  strcpy(adapterConfig.hostname, WiFi.getHostname());
#endif

  adapterConfig.localIP[0] = WiFi.localIP()[0];
  adapterConfig.localIP[1] = WiFi.localIP()[1];
  adapterConfig.localIP[2] = WiFi.localIP()[2];
  adapterConfig.localIP[3] = WiFi.localIP()[3];

  adapterConfig.gateway[0] = WiFi.gatewayIP()[0];
  adapterConfig.gateway[1] = WiFi.gatewayIP()[1];
  adapterConfig.gateway[2] = WiFi.gatewayIP()[2];
  adapterConfig.gateway[3] = WiFi.gatewayIP()[3];

  adapterConfig.netmask[0] = WiFi.subnetMask()[0];
  adapterConfig.netmask[1] = WiFi.subnetMask()[1];
  adapterConfig.netmask[2] = WiFi.subnetMask()[2];
  adapterConfig.netmask[3] = WiFi.subnetMask()[3];

  adapterConfig.dnsIP[0] = WiFi.dnsIP()[0];
  adapterConfig.dnsIP[1] = WiFi.dnsIP()[1];
  adapterConfig.dnsIP[2] = WiFi.dnsIP()[2];
  adapterConfig.dnsIP[3] = WiFi.dnsIP()[3];

  adapterConfig.macAddress[0] = WiFi.macAddress()[0];
  adapterConfig.macAddress[1] = WiFi.macAddress()[1];
  adapterConfig.macAddress[2] = WiFi.macAddress()[2];
  adapterConfig.macAddress[3] = WiFi.macAddress()[3];
  adapterConfig.macAddress[4] = WiFi.macAddress()[4];
  adapterConfig.macAddress[5] = WiFi.macAddress()[5];

  sio_to_computer(adapterConfig.rawData, sizeof(adapterConfig.rawData), false);
}

/**
   Scan for networks
*/
void sio_net_scan_networks()
{
  char ret[4] = {0, 0, 0, 0};

  // Scan to computer
  WiFi.mode(WIFI_STA);
  totalSSIDs = WiFi.scanNetworks();
  ret[0] = totalSSIDs;

  sio_to_computer((byte *)ret, 4, false);
}

/**
   Return scanned network entry
*/
void sio_net_scan_result()
{
  bool err = false;
  if (cmdFrame.aux1 < totalSSIDs)
  {
    strcpy(ssidInfo.ssid, WiFi.SSID(cmdFrame.aux1).c_str());
    ssidInfo.rssi = (char)WiFi.RSSI(cmdFrame.aux1);
  }
  else
  {
    memset(ssidInfo.rawData, 0x00, sizeof(ssidInfo.rawData));
    err = true;
  }


  sio_to_computer(ssidInfo.rawData, sizeof(ssidInfo.rawData), false);
}

/**
   Set SSID
*/
void sio_net_set_ssid()
{
  byte ck = sio_to_peripheral((byte *)&netConfig.rawData, sizeof(netConfig.rawData));

  if (sio_checksum(netConfig.rawData, sizeof(netConfig.rawData)) != ck)
  {
    sio_error();
  }
  else
  {
#ifdef DEBUG
    Debug_printf("Connecting to net: %s password: %s\r\n", netConfig.ssid, netConfig.password);
#endif
    WiFi.begin(netConfig.ssid, netConfig.password);
    UDP.begin(16384);
    sio_complete();
  }
}

/**
   SIO Status
*/
void sio_status()
{
  switch (cmdFrame.devic)
  {
    case 0x50:
      {
        byte status[2] = {0x00, 0x00};
        sio_to_computer(status, sizeof(status), false);
#ifdef DEBUG
        Debug_println("R: Status Complete");
#endif
        break;
      }
    default: // D:
      {
        byte status[4] = {0x10, 0xDF, 0xFE, 0x00};
        byte deviceSlot=cmdFrame.devic-0x31;
        
        if (sectorSize[deviceSlot]==256)
        {
          status[0]|=0x20;
        }

        if (percomBlock[deviceSlot].sectors_per_trackL==26)
        {
          status[0]|=0x80;  
        }
        
        sio_to_computer(status, sizeof(status), false);
        break;
      }
  }
}

/**
   SIO get WiFi Status
*/
void sio_net_get_wifi_status()
{
  char wifiStatus = WiFi.status();

  // Update WiFi Status LED
  if (wifiStatus == WL_CONNECTED)
    wifi_led(true);
  else
    wifi_led(false);

  sio_to_computer((byte *)&wifiStatus, 1, false);
}

/**
   Format Disk (fake)
*/
void sio_format()
{
  unsigned char deviceSlot = cmdFrame.devic - 0x31;

  // Populate bad sector map (no bad sectors)
  for (int i = 0; i < sectorSize[deviceSlot]; i++)
    sector[i] = 0;

  sector[0] = 0xFF; // no bad sectors.
  sector[1] = 0xFF;

  // Send to computer
  sio_to_computer((byte *)sector, sectorSize[deviceSlot], false);
}

/**
   SIO TNFS Server Mount
*/
void sio_tnfs_mount_host()
{
  unsigned char hostSlot = cmdFrame.aux1;
  bool err = tnfs_mount(hostSlot);

  if (!err)
    sio_error();
  else
    sio_complete();
}

/**
   SIO TNFS Disk Image Mount
*/
void sio_disk_image_mount()
{
  unsigned char deviceSlot = cmdFrame.aux1;
  unsigned char options = cmdFrame.aux2; // 1=R | 2=R/W | 128=FETCH
  unsigned short newss;
  unsigned short num_para;
  unsigned char num_para_hi;
  unsigned short num_sectors;
  bool opened = tnfs_open(deviceSlot, options);

  if (!opened)
  {
    sio_error();
  }
  else
  {
    // Get file and sector size from header
    tnfs_seek(deviceSlot, 2);
    tnfs_read(deviceSlot, 2);
    num_para = (256 * tnfsPacket.data[4]) + tnfsPacket.data[3];
    tnfs_read(deviceSlot, 2);
    newss = (256 * tnfsPacket.data[4]) + tnfsPacket.data[3];
    tnfs_read(deviceSlot, 1);
    num_para_hi = tnfsPacket.data[3];
    sectorSize[deviceSlot] = newss;
    num_sectors = para_to_num_sectors(num_para,num_para_hi,newss);
    derive_percom_block(deviceSlot, newss, num_sectors);
    sio_complete();
  }
}

/**
   SIO TNFS Disk Image uMount
*/
void sio_disk_image_umount()
{
  unsigned char deviceSlot = cmdFrame.aux1;
  bool opened = tnfs_close(deviceSlot);

  sio_complete(); // always completes.
}

/**
 * Convert # of paragraphs to sectors
 * para = # of paragraphs returned from ATR header
 * ss = sector size returned from ATR header
 */
unsigned short para_to_num_sectors(unsigned short para, unsigned char para_hi, unsigned short ss)
{
  unsigned long tmp=para_hi<<16;
  tmp|=para;
  
  unsigned short num_sectors=((tmp<<4)/ss);
  

#ifdef DEBUG_VERBOSE
  Debug_printf("ATR Header\n");
  Debug_printf("----------\n");
  Debug_printf("num paragraphs: $%04x\n",para);
  Debug_printf("Sector Size: %d\n",ss);
  Debug_printf("num sectors: %d\n",num_sectors);
#endif

  // Adjust sector size for the fact that the first three sectors are 128 bytes
  if (ss==256)
    num_sectors+=2;

  return num_sectors;
}

/**
   Update PERCOM block from the total # of sectors.
*/
void derive_percom_block(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors)
{
    // Start with 40T/1S 720 Sectors, sector size passed in
    percomBlock[deviceSlot].num_tracks = 40;
    percomBlock[deviceSlot].step_rate = 1;
    percomBlock[deviceSlot].sectors_per_trackM = 0;
    percomBlock[deviceSlot].sectors_per_trackL = 18;
    percomBlock[deviceSlot].num_sides = 1;
    percomBlock[deviceSlot].density = (sectorSize>128 ? 4 : 0); // >128 bytes = MFM
    percomBlock[deviceSlot].sector_sizeM=(sectorSize==256 ? 0x01 : 0x00);
    percomBlock[deviceSlot].sector_sizeL=(sectorSize==256 ? 0x00 : 0x80);
    percomBlock[deviceSlot].drive_present = 1;
    percomBlock[deviceSlot].reserved1 = 0;
    percomBlock[deviceSlot].reserved2 = 0;
    percomBlock[deviceSlot].reserved3 = 0;
  
    if (numSectors == 1040) // 1050 density
    {
      percomBlock[deviceSlot].sectors_per_trackM = 0;
      percomBlock[deviceSlot].sectors_per_trackL = 26;
      percomBlock[deviceSlot].density=4; // 1050 density is MFM, override.
    }
    else if (numSectors == 1440)
    {
      percomBlock[deviceSlot].num_sides = 2;
      percomBlock[deviceSlot].density=4; // DS/DD density is MFM, override.
    }
    else if (numSectors == 2880)
    {
      percomBlock[deviceSlot].num_sides = 2;
      percomBlock[deviceSlot].num_tracks = 80;
      percomBlock[deviceSlot].density=4; // DS/QD density is MFM, override.
    }
    else
    {
      // This is a custom size, one long track.
      percomBlock[deviceSlot].num_tracks=1;
      percomBlock[deviceSlot].sectors_per_trackM=numSectors>>8;
      percomBlock[deviceSlot].sectors_per_trackL=numSectors&0xFF;
    }

#ifdef DEBUG_VERBOSE
    Debug_printf("Percom block dump for newly mounted device slot %d\n",deviceSlot);
    dump_percom_block(deviceSlot);
#endif
}

/**
 * Dump PERCOM block
 */
void dump_percom_block(unsigned char deviceSlot)
{
#ifdef DEBUG_VERBOSE
  Debug_printf("Percom Block Dump\n");
  Debug_printf("-----------------\n");
  Debug_printf("Num Tracks: %d\n",percomBlock[deviceSlot].num_tracks);
  Debug_printf("Step Rate: %d\n",percomBlock[deviceSlot].step_rate);
  Debug_printf("Sectors per Track: %d\n",(percomBlock[deviceSlot].sectors_per_trackM*256+percomBlock[deviceSlot].sectors_per_trackL));
  Debug_printf("Num Sides: %d\n",percomBlock[deviceSlot].num_sides);
  Debug_printf("Density: %d\n",percomBlock[deviceSlot].density);
  Debug_printf("Sector Size: %d\n",(percomBlock[deviceSlot].sector_sizeM*256+percomBlock[deviceSlot].sector_sizeL));
  Debug_printf("Drive Present: %d\n",percomBlock[deviceSlot].drive_present);
  Debug_printf("Reserved1: %d\n",percomBlock[deviceSlot].reserved1);
  Debug_printf("Reserved2: %d\n",percomBlock[deviceSlot].reserved2);
  Debug_printf("Reserved3: %d\n",percomBlock[deviceSlot].reserved3);
#endif
}

/**
 * Read percom block
 */
void sio_read_percom_block()
{
  unsigned char deviceSlot = cmdFrame.devic - 0x31;
#ifdef DEBUG_VERBOSE
  dump_percom_block(deviceSlot);
#endif 
  sio_to_computer((byte *)&percomBlock[deviceSlot], 12, false);
  SIO_UART.flush();
}

/**
 * Write percom block
 */
void sio_write_percom_block()
{
  unsigned char deviceSlot = cmdFrame.devic - 0x31;
  sio_to_peripheral((byte *)&percomBlock[deviceSlot], 12);
#ifdef DEBUG_VERBOSE
  dump_percom_block(deviceSlot);
#endif
  sio_complete();  
}

/**
   Open TNFS Directory
*/
void sio_tnfs_open_directory()
{
  byte hostSlot = cmdFrame.aux1;
  byte ck = sio_to_peripheral((byte *)&current_entry, sizeof(current_entry));

  if (tnfs_opendir(hostSlot))
    sio_complete();
  else
    sio_error();
}

/**
   Read next TNFS Directory entry
*/
void sio_tnfs_read_directory_entry()
{
  byte hostSlot = cmdFrame.aux2;
  byte len = cmdFrame.aux1;
  byte ret = tnfs_readdir(hostSlot);

  if (!ret)
    current_entry[0] = 0x7F; // end of dir

  sio_to_computer((byte *)&current_entry, len, false);
}

/**
   Close TNFS Directory
*/
void sio_tnfs_close_directory()
{
  byte hostSlot = cmdFrame.aux1;

  if (tnfs_closedir(hostSlot))
    sio_complete();
  else
    sio_error();
}

/**
   (disk) High Speed
*/
void sio_high_speed()
{
  byte hsd = 0x0a; // 19200 standard speed

  sio_to_computer((byte *)&hsd, 1, false);
  SIO_UART.updateBaudRate(38908);

#ifdef DEBUG
  Debug_println("SIO HIGH SPEED");
#endif
}

/**
   Write hosts slots
*/
void sio_write_hosts_slots()
{
  byte ck = sio_to_peripheral(hostSlots.rawData, sizeof(hostSlots.rawData));

  if (sio_checksum(hostSlots.rawData, sizeof(hostSlots.rawData)) == ck)
  {
    atrConfig.seek(91792, SeekSet);
    atrConfig.write(hostSlots.rawData, sizeof(hostSlots.rawData));
    atrConfig.flush();
    sio_complete();
  }
  else
    sio_error();
}

/**
   Write Device slots
*/
void sio_write_device_slots()
{
  byte ck = sio_to_peripheral(deviceSlots.rawData, sizeof(deviceSlots.rawData));

  if (sio_checksum(deviceSlots.rawData, sizeof(deviceSlots.rawData)) == ck)
  {
    atrConfig.seek(91408, SeekSet);
    atrConfig.write(deviceSlots.rawData, sizeof(deviceSlots.rawData));
    atrConfig.flush();
    sio_complete();
  }
  else
    sio_error();
}

/**
   SIO Disk Write
*/
void sio_write()
{
  switch (cmdFrame.devic)
  {
    case 0x50: // R: Device
      // for now, just complete
      sio_complete();
      break;
    default: // D:
      byte ck;
      int ss; // sector size
      int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
      int sectorNum = offset;
      unsigned char deviceSlot = cmdFrame.devic - 0x31;

      if (sectorNum < 4)
      {
        // First three sectors are always single density
        offset *= 128;
        offset -= 128;
        offset += 16; // skip 16 byte ATR Header
        ss = 128;
      }
      else
      {
        // First three sectors are always single density
        offset *= sectorSize[deviceSlot];
        offset -= sectorSize[deviceSlot];
        offset += 16; // skip 16 byte ATR Header
        ss = sectorSize[deviceSlot];
      }

      ck = sio_to_peripheral(sector, ss);

      if (ck == sio_checksum(sector, ss))
      {
        if (load_config == true)
        {
          atrConfig.seek(offset, SeekSet);
          atrConfig.write(sector, ss);
          atrConfig.flush();
        }
        else
        {
          tnfs_seek(deviceSlot, offset);
          tnfs_write(deviceSlot, ss);
          firstCachedSector[cmdFrame.devic - 0x31] = 65535; // invalidate cache
        }
        sio_complete();
      }
      else
      {
        sio_error();
      }
      break;
  }
}

/**
   Read hosts Slots
*/
void sio_read_hosts_slots()
{
  sio_to_computer(hostSlots.rawData, sizeof(hostSlots.rawData), false);
}

/**
   Read Device Slots
*/
void sio_read_device_slots()
{
  load_config = false;
  sio_to_computer(deviceSlots.rawData, sizeof(deviceSlots.rawData), false);
}

/**
   SIO Disk read
*/
void sio_read()
{
  int ss;
  unsigned char deviceSlot = cmdFrame.devic - 0x31;
  int sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  int cacheOffset = 0;
  int offset;
  byte* s;
  byte* d;
  byte err = false;

  if (load_config == true) // no TNFS ATR mounted.
  {
    ss = 128;
    offset = sectorNum;
    offset *= 128;
    offset -= 128;
    offset += 16;
    atrConfig.seek(offset, SeekSet);
    atrConfig.read(sector, 128);
  }
  else // TNFS ATR mounted and opened...
  {
    max_cached_sectors = (sectorSize[deviceSlot] == 256 ? 9 : 19);
    if ((sectorNum > (firstCachedSector[deviceSlot] + max_cached_sectors)) || (sectorNum < firstCachedSector[deviceSlot])) // cache miss
    {
      firstCachedSector[deviceSlot] = sectorNum;
      cacheOffset = 0;

      if (sectorNum < 4)
        ss = 128; // First three sectors are always single density
      else
        ss = sectorSize[deviceSlot];

      offset = sectorNum;
      offset *= ss;
      offset -= ss;

      // Bias adjustment for 256 bytes
      if (ss == 256)
        offset -= 384;

      offset += 16;
#ifdef DEBUG
      Debug_printf("firstCachedSector: %d\r\n", firstCachedSector);
      Debug_printf("cacheOffset: %d\r\n", cacheOffset);
      Debug_printf("offset: %d\r\n", offset);
#endif
      tnfs_seek(deviceSlot, offset);
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset += 256;
      tnfs_read(deviceSlot, 256);
      s = &tnfsPacket.data[3];
      d = &sectorCache[deviceSlot][cacheOffset];
      memcpy(d, s, 256);
      cacheOffset = 0;
    }
    else // cache hit, adjust offset
    {
      if (sectorNum < 4)
        ss = 128;
      else
        ss = sectorSize[deviceSlot];

      cacheOffset = ((sectorNum - firstCachedSector[deviceSlot]) * ss);
#ifdef DEBUG
      Debug_printf("cacheOffset: %d\r\n", cacheOffset);
#endif
    }
    d = &sector[0];
    s = &sectorCache[deviceSlot][cacheOffset];
    memcpy(d, s, ss);
  }

  sio_to_computer((byte *)&sector, ss, err);
}

/**
   Drain data out of SIO port
*/
void sio_flush()
{
  while (SIO_UART.available())
  {
    SIO_UART.read(); // toss it.
#ifdef DEBUG
    Debug_printf(".");
#endif
  }
}

/**
   Mount the TNFS server
*/
bool tnfs_mount(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    memset(tnfsPacket.rawData, 0, sizeof(tnfsPacket.rawData));

    // Do not mount, if we already have a session ID, just bail.
    if (tnfsSessionIDs[hostSlot].session_idl != 0 && tnfsSessionIDs[hostSlot].session_idh != 0)
      return true;

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
    Debug_println((char*)hostSlots.host[hostSlot]);
#ifdef DEBUG_VERBOSE
    for (int i = 0; i < 32; i++)
      Debug_printf("%02x ", hostSlots.host[hostSlot][i]);
    Debug_printf("\r\n\r\n");
    Debug_print("Req Packet: ");
    for (int i = 0; i < 10; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif
#endif

    UDP.beginPacket(String(hostSlots.host[hostSlot]).c_str(), 16384);
    UDP.write(tnfsPacket.rawData, 10);
    UDP.endPacket();

#ifdef DEBUG
    Debug_println("Wrote the packet");
#endif

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
#ifdef DEBUG_VERBOSE
        Debug_print("Resp Packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
#ifdef DEBUG
          Debug_print("Successful, Session ID: ");
          Debug_print(tnfsPacket.session_idl, HEX);
          Debug_println(tnfsPacket.session_idh, HEX);
#endif
          // Persist the session ID.
          tnfsSessionIDs[hostSlot].session_idl = tnfsPacket.session_idl;
          tnfsSessionIDs[hostSlot].session_idh = tnfsPacket.session_idh;
          return true;
        }
        else
        {
          // Error
#ifdef DEBUG
          Debug_print("Error #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif
          return false;
        }
      }
    }
    // Otherwise we timed out.
#ifdef DEBUG
    Debug_println("Timeout after 5000ms");
#endif
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\r\n");
#endif
  return false;
}

/**
   Open 'autorun.atr'
*/
bool tnfs_open(unsigned char deviceSlot, unsigned char options)
{
  int start = millis();
  int dur = millis() - start;
  int c = 0;
  unsigned char retries = 0;

  while (retries < 5)
  {
    strcpy(mountPath, deviceSlots.slot[deviceSlot].file);
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.retryCount++;  // increase sequence #
    tnfsPacket.command = 0x29; // OPEN

    if (options == 0x01)
      tnfsPacket.data[c++] = 0x01;
    else if (options == 0x02)
      tnfsPacket.data[c++] = 0x03;
    else
      tnfsPacket.data[c++] = 0x03;

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
    Debug_printf("Opening /%s\r\n", mountPath);
    Debug_println("");
#ifdef DEBUG_VERBOSE
    Debug_print("Req Packet: ");
    for (int i = 0; i < c + 4; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
#endif
#endif

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, c + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          tnfs_fds[deviceSlot] = tnfsPacket.data[1];
#ifdef DEBUG
          Debug_print("Successful, file descriptor: #");
          Debug_println(tnfs_fds[deviceSlot], HEX);
#endif
          return true;
        }
        else
        {
          // unsuccessful
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif
          return false;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif
  }
#ifdef DEBUG
  Debug_printf("Failed\r\n");
#endif
  return false;
}

/**
   Open 'autorun.atr'
*/
bool tnfs_close(unsigned char deviceSlot)
{
  int start = millis();
  int dur = millis() - start;
  int c = 0;
  unsigned char retries = 0;

  while (retries < 5)
  {
    strcpy(mountPath, deviceSlots.slot[deviceSlot].file);
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.retryCount++;  // increase sequence #
    tnfsPacket.command = 0x23; // CLOSE

    tnfsPacket.data[c++] = tnfs_fds[deviceSlot];

    for (int i = 0; i < strlen(mountPath); i++)
    {
      tnfsPacket.data[i + 5] = mountPath[i];
      c++;
    }

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, c + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          return true;
        }
        else
        {
          // unsuccessful
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif
          return false;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif
  }
#ifdef DEBUG
  Debug_printf("Failed\r\n");
#endif
  return false;
}

/**
   TNFS Open Directory
*/
bool tnfs_opendir(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[hostSlot].session_idh;
    tnfsPacket.retryCount++;  // increase sequence #
    tnfsPacket.command = 0x10; // OPENDIR
    tnfsPacket.data[0] = '/'; // Open root dir
    tnfsPacket.data[1] = 0x00; // nul terminated

#ifdef DEBUG
    Debug_println("TNFS Open directory /");
#endif

    UDP.beginPacket(String(hostSlots.host[hostSlot]).c_str(), 16384);
    UDP.write(tnfsPacket.rawData, 2 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          tnfs_dir_fds[hostSlot] = tnfsPacket.data[1];
#ifdef DEBUG
          Debug_printf("Opened dir on slot #%d - fd = %02x\r\n", hostSlot, tnfs_dir_fds[hostSlot]);
#endif
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
#endif
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.");
#endif
  return false;
}

/**
   TNFS Read Directory
   Reads the next directory entry
*/
bool tnfs_readdir(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[hostSlot].session_idh;
    tnfsPacket.retryCount++;  // increase sequence #
    tnfsPacket.command = 0x11; // READDIR
    tnfsPacket.data[0] = tnfs_dir_fds[hostSlot]; // Open root dir

#ifdef DEBUG
    Debug_printf("TNFS Read next dir entry, slot #%d - fd %02x\r\n\r\n", hostSlot, tnfs_dir_fds[hostSlot]);
#endif

    UDP.beginPacket(String(hostSlots.host[hostSlot]).c_str(), 16384);
    UDP.write(tnfsPacket.rawData, 1 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
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
#endif
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\r\n");
#endif
}

/**
   TNFS Close Directory
*/
bool tnfs_closedir(unsigned char hostSlot)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[hostSlot].session_idh;
    tnfsPacket.retryCount++;  // increase sequence #
    tnfsPacket.command = 0x12; // CLOSEDIR
    tnfsPacket.data[0] = tnfs_dir_fds[hostSlot]; // Open root dir

#ifdef DEBUG
    Debug_println("TNFS dir close");
#endif

    UDP.beginPacket(hostSlots.host[hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 1 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, 516);
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
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
    retries++;
    tnfsPacket.retryCount--;
#endif
  }
#ifdef DEBUG
  Debug_printf("Failed.\r\n");
#endif
  return false;
}

/**
   TNFS write
*/
bool tnfs_write(unsigned char deviceSlot, unsigned short len)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.retryCount++;  // Increase sequence
    tnfsPacket.command = 0x22; // READ
    tnfsPacket.data[0] = tnfs_fds[deviceSlot]; // returned file descriptor
    tnfsPacket.data[1] = len & 0xFF;
    tnfsPacket.data[2] = len >> 8;

#ifdef DEBUG
    Debug_print("Writing to File descriptor: ");
    Debug_println(tnfs_fds[deviceSlot]);
#ifdef DEBUG_VERBOSE
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif
#endif

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.write(sector, 128);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
#ifdef DEBUG
          Debug_println("Successful.");
#endif
          return true;
        }
        else
        {
          // Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\r\n");
#endif
}

/**
   TNFS read
*/
bool tnfs_read(unsigned char deviceSlot, unsigned short len)
{
  int start = millis();
  int dur = millis() - start;
  unsigned char retries = 0;

  while (retries < 5)
  {
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.retryCount++;  // Increase sequence
    tnfsPacket.command = 0x21; // READ
    tnfsPacket.data[0] = tnfs_fds[deviceSlot]; // returned file descriptor
    tnfsPacket.data[1] = len & 0xFF; // len bytes
    tnfsPacket.data[2] = len >> 8; //

#ifdef DEBUG
    Debug_print("Reading from File descriptor: ");
    Debug_println(tnfs_fds[deviceSlot]);
#ifdef DEBUG_VERBOSE
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif
#endif

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.endPacket();
    start = millis();
    dur = millis() - start;
    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
#ifdef DEBUG
          Debug_println("Successful.");
#endif
          return true;
        }
        else
        {
          // Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    if (retries < 5)
      Debug_printf("Retrying...\r\n");
#endif
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\r\n");
#endif
  return false;
}

/**
   TNFS seek
*/
bool tnfs_seek(unsigned char deviceSlot, long offset)
{
  int start = millis();
  int dur = millis() - start;
  byte offsetVal[4];
  unsigned char retries = 0;

  while (retries < 5)
  {
    offsetVal[0] = (int)((offset & 0xFF000000) >> 24 );
    offsetVal[1] = (int)((offset & 0x00FF0000) >> 16 );
    offsetVal[2] = (int)((offset & 0x0000FF00) >> 8 );
    offsetVal[3] = (int)((offset & 0X000000FF));

    tnfsPacket.retryCount++;
    tnfsPacket.session_idl = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idl;
    tnfsPacket.session_idh = tnfsSessionIDs[deviceSlots.slot[deviceSlot].hostSlot].session_idh;
    tnfsPacket.command = 0x25; // LSEEK
    tnfsPacket.data[0] = tnfs_fds[deviceSlot];
    tnfsPacket.data[1] = 0x00; // SEEK_SET
    tnfsPacket.data[2] = offsetVal[3];
    tnfsPacket.data[3] = offsetVal[2];
    tnfsPacket.data[4] = offsetVal[1];
    tnfsPacket.data[5] = offsetVal[0];

#ifdef DEBUG
    Debug_print("Seek requested to offset: ");
    Debug_println(offset);
#ifdef DEBUG_VERBOSE
    Debug_print("Req packet: ");
    for (int i = 0; i < 10; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif
#endif

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 6 + 4);
    UDP.endPacket();

    while (dur < 5000)
    {
      dur = millis() - start;
      yield();
      if (UDP.parsePacket())
      {
        int l = UDP.read(tnfsPacket.rawData, sizeof(tnfsPacket.rawData));
#ifdef DEBUG_VERBOSE
        Debug_print("Resp packet: ");
        for (int i = 0; i < l; i++)
        {
          Debug_print(tnfsPacket.rawData[i], HEX);
          Debug_print(" ");
        }
        Debug_println("");
#endif

        if (tnfsPacket.data[0] == 0)
        {
          // Success.
#ifdef DEBUG
          Debug_println("Successful.");
#endif
          return true;
        }
        else
        {
          // Error.
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    if (retries < 5)
      Debug_printf("Retrying...\r\n");
#endif
    tnfsPacket.retryCount--;
    retries++;
  }
#ifdef DEBUG
  Debug_printf("Failed.\r\n");
#endif
  return false;
}

void sio_wait()
{
  SIO_UART.read(); // Toss it for now
}

/**
 ** 850 Control 0x41

   DTR/RTS/XMT
  D7 Enable DTR (Data Terminal Ready) change
  D5 Enable RTS (Request To Send) change
  D1 Enable XMT (Transmit) change
      0 No change
      1 Change state
  D6 New DTR state (if D7 set)
  D4 New RTS state (if D5 set)
  D0 New XMT state (if D1 set)
      0 Negate / space
*/
void sio_R_control()
{
  // for now, just complete
  sio_complete();

#ifdef DEBUG
  Debug_println("R:Control: Sent Complete");
#endif
}

/**
   850 Configure (0x42)
*/
void sio_R_config()
{
  sio_complete();

  // Plz verify the bitshifting for wordsize and stopbit - mozz
  byte newBaud = 0x0F & cmdFrame.aux1; // Get baud rate
  byte wordSize = 0x30 & cmdFrame.aux1; // Get word size
  byte stopBit = (1 << 7) & cmdFrame.aux1; // Get stop bits, 0x80 = 2, 0 = 1

  switch (newBaud)
  {
    case 0x08:
      modemBaud = 300;
      break;
    case 0x09:
      modemBaud = 600;
      break;
    case 0xA:
      modemBaud = 1200;
      break;
    case 0x0B:
      modemBaud = 1800;
      break;
    case 0x0C:
      modemBaud = 2400;
      break;
    case 0x0D:
      modemBaud = 4800;
      break;
    case 0x0E:
      modemBaud = 9600;
      break;
    case 0x0F:
      modemBaud = 19200;
      break;
  }

#ifdef DEBUG
  Debug_printf("R:Config: %i, %X, ", wordSize, stopBit);
  Debug_println(modemBaud);
#endif

}

/**
   850 Concurrent mode
*/
void sio_R_concurrent()
{
  char response[] = {0x28, 0xA0, 0x00, 0xA0, 0x28, 0xA0, 0x00, 0xA0, 0x78}; // 19200

  switch (modemBaud)
  {
    case 300:
      response[0]=response[4]=0xA0;
      response[2]=response[6]=0x0B;
      break;
    case 600:
      response[0]=response[4]=0xCC;
      response[2]=response[6]=0x05;
      break;
    case 1200:
      response[0]=response[4]=0xE3;
      response[2]=response[6]=0x02;
      break;
    case 1800:
      response[0]=response[4]=0xEA;
      response[2]=response[6]=0x01;
      break;
    case 2400:
      response[0]=response[4]=0x6E;
      response[2]=response[6]=0x01;
      break;
    case 4800:
      response[0]=response[4]=0xB3;
      response[2]=response[6]=0x00;
      break;
    case 9600:
      response[0]=response[4]=0x56;
      response[2]=response[6]=0x00;
      break;
    case 19200:
      response[0]=response[4]=0x28;
      response[2]=response[6]=0x00;
      break;
  }

  sio_to_computer((byte *)response,sizeof(response),false);

#ifndef ESP32
  SIO_UART.flush(); // ok, WHY?
#endif

#ifdef DEBUG
  Debug_println("R:Stream: Start");
#endif

  modemActive = true;
  SIO_UART.updateBaudRate(modemBaud);
#ifdef DEBUG
  Debug_print("MODEM ACTIVE @");
  Debug_println(modemBaud);
#endif
}

/**
   Perform a command given in command mode
*/
void modemCommand()
{
  cmd.trim();
  if (cmd == "") return;
  SIO_UART.println();
  String upCmd = cmd;
  upCmd.toUpperCase();

  long newBps = 0;

  // Replace EOL with CR.
  if (upCmd.indexOf(0x9b) != 0)
    upCmd[upCmd.indexOf(0x9b)] = 0x0D;

  /**** Just AT ****/
  if (upCmd == "AT") SIO_UART.println("OK");

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0))
  {
    int portIndex = cmd.indexOf(":");
    String host, port;
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
    }
#ifdef DEBUG
    Debug_print("DIALING: ");
    Debug_println(host);
#endif
    if (host == "5551234") // Fake it for BobTerm
    {
      delay(1300); // Wait a moment so bobterm catches it
      SIO_UART.print("CONNECT ");
      SIO_UART.println(modemBaud);
#ifdef ESP32
      SIO_UART.flush();
#endif
#ifdef DEBUG
      Debug_println("CONNECT FAKE!");
#endif
    }
    else
    {
      SIO_UART.print("Connecting to ");
      SIO_UART.print(host);
      SIO_UART.print(":");
      SIO_UART.println(port);
      char *hostChr = new char[host.length() + 1];
      host.toCharArray(hostChr, host.length() + 1);
      int portInt = port.toInt();
      tcpClient.setNoDelay(true); // Try to disable naggle
      if (tcpClient.connect(hostChr, portInt))
      {
        tcpClient.setNoDelay(true); // Try to disable naggle
        SIO_UART.print("CONNECT ");
        SIO_UART.println(modemBaud);
        cmdMode = false;
#ifdef ESP32
        SIO_UART.flush();
#endif
        if (listenPort > 0) tcpServer.stop();
      }
      else
      {
        SIO_UART.println("NO CARRIER");
      }
      delete hostChr;
    }
  }

  /**** Connect to WIFI ****/
  else if (upCmd.indexOf("ATWIFI") == 0)
  {
    int keyIndex = cmd.indexOf(",");
    String ssid, key;
    if (keyIndex != -1)
    {
      ssid = cmd.substring(6, keyIndex);
      key = cmd.substring(keyIndex + 1, cmd.length());
    }
    else
    {
      ssid = cmd.substring(6, cmd.length());
      key = "";
    }
    char *ssidChr = new char[ssid.length() + 1];
    ssid.toCharArray(ssidChr, ssid.length() + 1);
    char *keyChr = new char[key.length() + 1];
    key.toCharArray(keyChr, key.length() + 1);
    SIO_UART.print("Connecting to ");
    SIO_UART.print(ssid);
    SIO_UART.print("/");
    SIO_UART.println(key);
    WiFi.begin(ssidChr, keyChr);
    for (int i = 0; i < 100; i++)
    {
      delay(100);
      if (WiFi.status() == WL_CONNECTED)
      {
        SIO_UART.println("OK");
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED)
    {
      SIO_UART.println("ERROR");
    }
    delete ssidChr;
    delete keyChr;
  }

  /**** Change baud rate from default ****/
  else if (upCmd == "AT300") newBps = 300;
  else if (upCmd == "AT1200") newBps = 1200;
  else if (upCmd == "AT2400") newBps = 2400;
  else if (upCmd == "AT4800") newBps = 4800;
  else if (upCmd == "AT9600") newBps = 9600;
  else if (upCmd == "AT19200") newBps = 19200;
  else if (upCmd == "AT38400") newBps = 38400;
  else if (upCmd == "AT57600") newBps = 57600;
  else if (upCmd == "AT115200") newBps = 115200;

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    SIO_UART.println("OK");
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    SIO_UART.println("OK");
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient())
  {
    tcpClient = tcpServer.available();
    tcpClient.setNoDelay(true); // try to disable naggle
    tcpServer.stop();
    SIO_UART.print("CONNECT ");
    SIO_UART.println(modemBaud);
    cmdMode = false;
#ifdef ESP32
    SIO_UART.flush();
#endif
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP")
  {
    SIO_UART.println(WiFi.localIP());
    SIO_UART.println("OK");
  }

  /**** Print Help ****/
  else if (upCmd == "AT?")
  {
    SIO_UART.println("       FujiNet Virtual Modem 850");
    SIO_UART.println("=======================================");
    SIO_UART.println();
    SIO_UART.println("ATWIFI<ssid>,<key> | Connect to WIFI");
    //SIO_UART.println("AT<baud>           | Change Baud Rate");
    SIO_UART.println("ATDT<host>:<port>  | Connect by TCP");
    SIO_UART.println("ATIP               | See my IP address");
    SIO_UART.println("ATNET0             | Disable telnet");
    SIO_UART.println("                   | command handling");
    SIO_UART.println("ATPORT<port>       | Set listening port");
    SIO_UART.println("ATGET<URL>         | HTTP GET");
    SIO_UART.println();
    if (listenPort > 0)
    {
      SIO_UART.print("Listening to connections on port ");
      SIO_UART.println(listenPort);
      SIO_UART.println("which result in RING and you can");
      SIO_UART.println("answer with ATA.");
      tcpServer.begin(listenPort);
    }
    else
    {
      SIO_UART.println("Incoming connections are disabled.");
    }
    SIO_UART.println("");
    SIO_UART.println("OK");
  }

  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12); // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12); // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0)
    {
      pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
      port = 80;
      portIndex = pathIndex;
    }
    else
    {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Debug
    SIO_UART.print("Getting path ");
    SIO_UART.print(path);
    SIO_UART.print(" from port ");
    SIO_UART.print(port);
    SIO_UART.print(" of host ");
    SIO_UART.print(host);
    SIO_UART.println("...");

    // Establish connection
    if (!tcpClient.connect(hostChr, port))
    {
      SIO_UART.println("NO CARRIER");
    }
    else
    {
      SIO_UART.print("CONNECT ");
      SIO_UART.println(modemBaud);
      cmdMode = false;

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      tcpClient.print(request);
    }
    delete hostChr;
  }

  /**** Set Listening Port ****/
  else if (upCmd.indexOf("ATPORT") == 0)
  {
    long port;
    port = cmd.substring(6).toInt();
    if (port > 65535 || port < 0)
    {
      SIO_UART.println("ERROR");
    }
    else
    {
      listenPort = port;
      tcpServer.begin(listenPort);
      SIO_UART.println("OK");
    }
  }

  /**** Unknown command ****/
  else SIO_UART.println("ERROR");

  /**** Tasks to do after command has been parsed ****/
  if (newBps)
  {
    SIO_UART.println("OK");
    delay(150); // Sleep enough for 4 bytes at any previous baud rate to finish ("\nOK\n")
    SIO_UART.updateBaudRate(newBps);
    modemBaud = newBps;
  }

  cmd = "";
}

void setup()
{
#ifdef DEBUG_S
  BUG_UART.begin(115200);
  Debug_println();
  Debug_println(TEST_NAME);
#endif
  SPIFFS.begin();
  atrConfig = SPIFFS.open("/autorun.atr", "r+");

  // Go ahead and read the host slots from disk
  atrConfig.seek(91792, SeekSet);
  atrConfig.read(hostSlots.rawData, 256);

  // And populate the device slots
  atrConfig.seek(91408, SeekSet);
  atrConfig.read(deviceSlots.rawData, 304);

  // Go ahead and mark all device slots local
  for (int i = 0; i < 8; i++)
  {
    if (deviceSlots.slot[i].file[0] == 0x00)
    {
      deviceSlots.slot[i].hostSlot = 0xFF;
    }
  }

  // Set up pins
  pinMode(PIN_INT, OUTPUT);
  digitalWrite(PIN_INT, HIGH);
  pinMode(PIN_PROC, OUTPUT);
  digitalWrite(PIN_PROC, HIGH);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
  pinMode(PIN_CKI, OUTPUT);
  digitalWrite(PIN_CKI, LOW);
#if defined(ESP8266) && !defined(DEBUG_S)
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED2, HIGH); // off
#endif
#ifdef ESP32
  pinMode(PIN_CKO, INPUT);
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif

  // Set up serial
  SIO_UART.begin(19200);
#ifdef ESP8266
  SIO_UART.swap();
#endif

  // Set up SIO command function pointers
  for (int i = 0; i < 256; i++)
    cmdPtr[i] = sio_wait;

  cmdPtr['P'] = sio_write; // 0x50
  cmdPtr['W'] = sio_write; // 0x57
  cmdPtr['R'] = sio_read; // 0x52
  cmdPtr['S'] = sio_status; // 0x53
  cmdPtr['!'] = sio_format; //0x21
  cmdPtr[0x4E] = sio_read_percom_block;
  cmdPtr[0x4F] = sio_write_percom_block;
  cmdPtr[0x3F] = sio_high_speed;
  cmdPtr[0xFD] = sio_net_scan_networks;
  cmdPtr[0xFC] = sio_net_scan_result;
  cmdPtr[0xFB] = sio_net_set_ssid;
  cmdPtr[0xFA] = sio_net_get_wifi_status;
  cmdPtr[0xF9] = sio_tnfs_mount_host;
  cmdPtr[0xF8] = sio_disk_image_mount;
  cmdPtr[0xF7] = sio_tnfs_open_directory;
  cmdPtr[0xF6] = sio_tnfs_read_directory_entry;
  cmdPtr[0xF5] = sio_tnfs_close_directory;
  cmdPtr[0xF4] = sio_read_hosts_slots;
  cmdPtr[0xF3] = sio_write_hosts_slots;
  cmdPtr[0xF2] = sio_read_device_slots;
  cmdPtr[0xF1] = sio_write_device_slots;
  cmdPtr[0xE9] = sio_disk_image_umount;
  cmdPtr[0xE8] = sio_get_adapter_config;
  cmdPtr['B'] = sio_R_config; // 0x42
  cmdPtr['A'] = sio_R_control; // 0x41
  cmdPtr['X'] = sio_R_concurrent; // 0x58

  // Go ahead and flush anything out of the serial port
  sio_flush();

#ifdef DEBUG_N
  WiFi.begin(DEBUG_SSID, DEBUG_PASSWORD);
#elif defined(ESP32)
  WiFi.begin();
#endif
}

void loop()
{
#ifdef DEBUG_N
  /* Connect to debug server if we aren't and WiFi is connected */
  if ( !wificlient.connected() && WiFi.status() == WL_CONNECTED )
  {
    wificlient.connect(DEBUG_HOST, 6502);
    wificlient.println(TEST_NAME);
  }
#endif

  int a, c;
  if (digitalRead(PIN_CMD) == LOW)
  {
    if (modemActive)
    { // We were in modem mode. change baud for SIO
      SIO_UART.updateBaudRate(sioBaud);
      modemActive = false;
    }
    sio_led(true);
    memset(cmdFrame.cmdFrameData, 0, 5); // clear cmd frame.
#ifdef ESP8266
    //    delayMicroseconds(DELAY_T0); // computer is waiting for us to notice.
#endif

    // read cmd frame
    SIO_UART.readBytes(cmdFrame.cmdFrameData, 5);
#ifdef DEBUG
    Debug_printf("CMD Frame: %02x %02x %02x %02x %02x\r\n", cmdFrame.devic, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2, cmdFrame.cksum);
#endif

#ifdef ESP8266
    //    delayMicroseconds(DELAY_T1);
#endif

    // Wait for CMD line to raise again.
    while (!digitalRead(PIN_CMD))
      yield();

#ifdef ESP8266
    //    delayMicroseconds(DELAY_T2);
#endif

    if (sio_valid_device_id())
    {
      if ((cmdFrame.comnd == 0x3F))
      {
        sio_nak();
      }
      else
      {
        sio_ack();
#ifdef ESP8266
        delayMicroseconds(DELAY_T3);
#endif
        cmdPtr[cmdFrame.comnd]();
      }
      sio_led(false);
    }
  }
  else if (modemActive)
  {
    /**** AT command mode ****/
    if (cmdMode == true)
    {
      // In command mode but new unanswered incoming connection on server listen socket
      if ((listenPort > 0) && (tcpServer.hasClient()))
      {
        // Print RING every now and then while the new incoming connection exists
        if ((millis() - lastRingMs) > RING_INTERVAL)
        {
          SIO_UART.println("RING");
          lastRingMs = millis();
        }
      }

      // In command mode - don't exchange with TCP but gather characters to a string
      if (SIO_UART.available())
      {
        char chr = SIO_UART.read();

        // Return, enter, new line, carriage return.. anything goes to end the command
        if ((chr == '\n') || (chr == '\r') || (chr == 0x9B))
        {
#ifdef DEBUG
          Debug_print(cmd);
          Debug_println(" | CR");
#endif
          modemCommand();
        }
        // Backspace or delete deletes previous character
        else if ((chr == 8) || (chr == 127))
        {
          cmd.remove(cmd.length() - 1);
          // We don't assume that backspace is destructive
          // Clear with a space
          SIO_UART.write(8);
          SIO_UART.write(' ');
          SIO_UART.write(8);
        }
        else
        {
          if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
          SIO_UART.print(chr);
        }
      }
    }
    /**** Connected mode ****/
    else
    {
      // Transmit from terminal to TCP
      if (SIO_UART.available())
      {
        //led_on();

        // In telnet in worst case we have to escape every byte
        // so leave half of the buffer always free
        int max_buf_size;
        if (telnet == true)
          max_buf_size = TX_BUF_SIZE / 2;
        else
          max_buf_size = TX_BUF_SIZE;

        // Read from serial, the amount available up to
        // maximum size of the buffer
        size_t len = std::min(SIO_UART.available(), max_buf_size);
        SIO_UART.readBytes(&txBuf[0], len);

        // Disconnect if going to AT mode with "+++" sequence
        for (int i = 0; i < (int)len; i++)
        {
          if (txBuf[i] == '+') plusCount++; else plusCount = 0;
          if (plusCount >= 3)
          {
            plusTime = millis();
          }
          if (txBuf[i] != '+')
          {
            plusCount = 0;
          }
        }

        // Double (escape) every 0xff for telnet, shifting the following bytes
        // towards the end of the buffer from that point
        if (telnet == true)
        {
          for (int i = len - 1; i >= 0; i--)
          {
            if (txBuf[i] == 0xff)
            {
              for (int j = TX_BUF_SIZE - 1; j > i; j--)
              {
                txBuf[j] = txBuf[j - 1];
              }
              len++;
            }
          }
        }

        // Write the buffer to TCP finally
        tcpClient.write(&txBuf[0], len);
        yield();
      }

      // Transmit from TCP to terminal
      if (tcpClient.available())
      {
        //led_on();
        char buf[128];
        int avail = tcpClient.available();
        int i;

        if (avail > 128)
        {
          tcpClient.readBytes(buf, 128);
          for (i = 0; i < 128; i++)
            SIO_UART.write(buf[i]);
        }
        else
        {
          tcpClient.readBytes(buf, avail);
          for (i = 0; i < avail; i++)
            SIO_UART.write(buf[i]);
        }
      }
    }

    // If we have received "+++" as last bytes from serial port and there
    // has been over a second without any more bytes, disconnect
    if (plusCount >= 3)
    {
      if (millis() - plusTime > 1000)
      {
        tcpClient.stop();
        plusCount = 0;
      }
    }

    // Go to command mode if TCP disconnected and not in command mode
    if ((!tcpClient.connected()) && (cmdMode == false))
    {
      cmdMode = true;
      SIO_UART.println("NO CARRIER");
      if (listenPort > 0) tcpServer.begin();
    }
  }
  else
  {
    sio_led(false);
    a = SIO_UART.available();
    for (c = 0; c < a; c++)
      SIO_UART.read(); // dump it.
#ifdef DEBUG
    if (a > 0)
    {
      Debug_print("DUMPED BYTES: ");
      Debug_println(a);
    }
#endif
  }
}
