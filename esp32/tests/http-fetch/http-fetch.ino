/**
   Test #32 - Multilator Rev2 Rewrite
*/

#define TEST_NAME "#FujiNet Multi-Diskulator"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#endif
#ifdef ESP32
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#endif

#include <FS.h>
#include <WiFiUdp.h>

// Uncomment for Debug on 2nd UART (GPIO 2)
#define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
// #define DEBUG_N
// #define DEBUG_HOST ""
// #define DEBUG_SSID ""
// #define DEBUG_PASSWORD ""

#define DEBUG_VERBOSE 1

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

#define DELAY_T0  750
#define DELAY_T1  650
#define DELAY_T2  0
#define DELAY_T3  1000
#define DELAY_T4  850
#define DELAY_T5  250

bool hispeed = false;
int command_frame_counter = 0;
#define COMMAND_FRAME_SPEED_CHANGE_THRESHOLD 2
#define HISPEED_INDEX 0x0A
#define HISPEED_BAUDRATE 52640
#define STANDARD_BAUDRATE 19200
#define SERIAL_TIMEOUT 300

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

union
{
  struct
  {
    unsigned short numSectors;
    unsigned short sectorSize;
    unsigned char hostSlot;
    unsigned char deviceSlot;
    char filename[36];
  };
  unsigned char rawData[42];
} newDisk;

union
{
  struct
  {
    unsigned char magic1;
    unsigned char magic2;
    unsigned char filesizeH;
    unsigned char filesizeL;
    unsigned char secsizeH;
    unsigned char secsizeL;
    unsigned char filesizeHH;
    unsigned char res0;
    unsigned char res1;
    unsigned char res2;
    unsigned char res3;
    unsigned char res4;
    unsigned char res5;
    unsigned char res6;
    unsigned char res7;
    unsigned char flags;
  };
  unsigned char rawData[16];
} atrHeader;

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
HTTPClient http;
char url[80];

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
#ifdef ESP8266
  digitalWrite(PIN_LED, (onOff ? LOW : HIGH));
#elif defined(ESP32)
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
  else if (cmdFrame.devic == 0x70) // respond to FujiNet Network device commands
    return true;
  else if (cmdFrame.devic == 0x4F) // Do not respond to Type 3/4 polls
    return false;
  else if (cmdFrame.devic > 0x30 && cmdFrame.devic < 0x39)
  {
    if (deviceSlots.slot[deviceSlot].hostSlot != 0xFF)
      return true;
    else
      return false;
  }
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
}

/**
   sio COMPLETE
*/
void sio_complete()
{
  delayMicroseconds(DELAY_T5);
  SIO_UART.write('C');
}

/**
   sio ERROR
*/
void sio_error()
{
  delayMicroseconds(DELAY_T5);
  SIO_UART.write('E');
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
  delayMicroseconds(DELAY_T5);
#endif

  if (err == true)
    sio_error();
  else
    sio_complete();

  // Write data frame.
  SIO_UART.write(b, len);

  // Write checksum
  SIO_UART.write(ck);

#ifdef DEBUG_VERBOSE
  Debug_printf("TO COMPUTER: ");
  for (int i = 0; i < len; i++)
    Debug_printf("%02x ", b[i]);
  Debug_printf("\nCKSUM: %02x\n\n", ck);
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
  Debug_printf("l: %d\n", l);
  Debug_printf("TO PERIPHERAL: ");
  for (int i = 0; i < len; i++)
    Debug_printf("%02x ", sector[i]);
  Debug_printf("\nCKSUM: %02x\n\n", ck);
#endif

#ifdef ESP8266
  delayMicroseconds(DELAY_T4);
#endif

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
 * Set Base URL
 */
void sio_set_base_url()
{
  memset(url,0x00,sizeof(url));
  byte ck = sio_to_peripheral((byte *)&url, sizeof(url));
#ifdef DEBUG
  Debug_printf("\nBase URL now: %s\n",url);
#endif
  sio_complete();  
}

/**
   HTTP Open
*/
void sio_http_open()
{
  char file[80];
  char finalurl[80];
  byte ck = sio_to_peripheral((byte *)&file, sizeof(file));

  memset(&finalurl,0x00,sizeof(finalurl));
  strcat(finalurl,url);
  strcat(finalurl,file);

#ifdef DEBUG
  Debug_printf("\nAttempting HTTP GET for URL: %s\n",url);
#endif
  
  if (ck == sio_checksum((byte *)&file, sizeof(file)))
  {
    // Temporary, final version will store root certs in spiffs.
    http.end();
    http.begin(finalurl);
    int resultCode = http.GET();

#ifdef DEBUG_VERBOSE
    Debug_printf("Result code: %d\n", resultCode);
#endif

    if (resultCode == 200)
      sio_complete();
    else
      sio_error();
  }
  else // Checksum mismatch
  {
    sio_error();
  }
}

/**
   HTTP Get Characters
*/
void sio_http_get()
{
  bool err = false;
  WiFiClient* c;

  memset(sector, 0x00, sizeof(sector));

  c = http.getStreamPtr();
  if (c->available())
    c->read(sector, 256);

  sio_to_computer(sector, sizeof(sector), err);
}

/**
   HTTP Close
*/
void sio_http_close()
{
  http.end();
  sio_complete();
}

/**
   Make new disk and shove into device slot
*/
void sio_new_disk()
{
  byte ck = sio_to_peripheral(newDisk.rawData, sizeof(newDisk));

  if (ck == sio_checksum(newDisk.rawData, sizeof(newDisk)))
  {
    deviceSlots.slot[newDisk.deviceSlot].hostSlot = newDisk.hostSlot;
    deviceSlots.slot[newDisk.deviceSlot].mode = 0x03; // R/W
    strcpy(deviceSlots.slot[newDisk.deviceSlot].file, newDisk.filename);

    if (tnfs_open(newDisk.deviceSlot, 0x03, true) == true) // create file
    {
#ifdef DEBUG
      Debug_printf("XXX Created file %s\n", deviceSlots.slot[newDisk.deviceSlot].file);
#endif
      if (tnfs_write_blank_atr(newDisk.deviceSlot, newDisk.sectorSize, newDisk.numSectors) == true)
      {
#ifdef DEBUG
        Debug_printf("XXX Wrote ATR data\n");
#endif
        sectorSize[newDisk.deviceSlot] = newDisk.sectorSize;
        derive_percom_block(newDisk.deviceSlot, newDisk.sectorSize, newDisk.numSectors);
        sio_complete();
        return;
      }
      else
      {
#ifdef DEBUG
        Debug_printf("XXX ATR data write failed.\n");
#endif
        sio_error();
        return;
      }
    }
    else
    {
#ifdef DEBUG
      Debug_printf("XXX Could not open file %s\n", deviceSlots.slot[newDisk.deviceSlot].file);
#endif
      sio_error();
      return;
    }
  }
  else
  {
#ifdef DEBUG
    Debug_printf("XXX Bad Checksum.\n");
#endif
    sio_error();
    return;
  }
}

/**
   Get Adapter config.
*/
void sio_get_adapter_config()
{
  byte mac[6];
  strcpy(adapterConfig.ssid, netConfig.ssid);

#ifdef ESP8266
  strcpy(adapterConfig.hostname, WiFi.hostname().c_str());
#else
  strcpy(adapterConfig.hostname, WiFi.getHostname());
#endif

  WiFi.macAddress(mac);

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

  adapterConfig.macAddress[0] = mac[0]; // _WHY_ ?!
  adapterConfig.macAddress[1] = mac[1];
  adapterConfig.macAddress[2] = mac[2];
  adapterConfig.macAddress[3] = mac[3];
  adapterConfig.macAddress[4] = mac[4];
  adapterConfig.macAddress[5] = mac[5];

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
    Debug_printf("Connecting to net: %s password: %s\n", netConfig.ssid, netConfig.password);
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
  byte status[4] = {0x10, 0xDF, 0xFE, 0x00};
  byte deviceSlot = cmdFrame.devic - 0x31;

  if (sectorSize[deviceSlot] == 256)
  {
    status[0] |= 0x20;
  }

  if (percomBlock[deviceSlot].sectors_per_trackL == 26)
  {
    status[0] |= 0x80;
  }

  sio_to_computer(status, sizeof(status), false); // command always completes.
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
   Dump PERCOM block
*/
void dump_percom_block(unsigned char deviceSlot)
{
#ifdef DEBUG_VERBOSE
  Debug_printf("Percom Block Dump\n");
  Debug_printf("-----------------\n");
  Debug_printf("Num Tracks: %d\n", percomBlock[deviceSlot].num_tracks);
  Debug_printf("Step Rate: %d\n", percomBlock[deviceSlot].step_rate);
  Debug_printf("Sectors per Track: %d\n", (percomBlock[deviceSlot].sectors_per_trackM * 256 + percomBlock[deviceSlot].sectors_per_trackL));
  Debug_printf("Num Sides: %d\n", percomBlock[deviceSlot].num_sides);
  Debug_printf("Density: %d\n", percomBlock[deviceSlot].density);
  Debug_printf("Sector Size: %d\n", (percomBlock[deviceSlot].sector_sizeM * 256 + percomBlock[deviceSlot].sector_sizeL));
  Debug_printf("Drive Present: %d\n", percomBlock[deviceSlot].drive_present);
  Debug_printf("Reserved1: %d\n", percomBlock[deviceSlot].reserved1);
  Debug_printf("Reserved2: %d\n", percomBlock[deviceSlot].reserved2);
  Debug_printf("Reserved3: %d\n", percomBlock[deviceSlot].reserved3);
#endif
}

/**
   Convert # of paragraphs to sectors
   para = # of paragraphs returned from ATR header
   ss = sector size returned from ATR header
*/
unsigned short para_to_num_sectors(unsigned short para, unsigned char para_hi, unsigned short ss)
{
  unsigned long tmp = para_hi << 16;
  tmp |= para;

  unsigned short num_sectors = ((tmp << 4) / ss);


#ifdef DEBUG_VERBOSE
  Debug_printf("ATR Header\n");
  Debug_printf("----------\n");
  Debug_printf("num paragraphs: $%04x\n", para);
  Debug_printf("Sector Size: %d\n", ss);
  Debug_printf("num sectors: %d\n", num_sectors);
#endif

  // Adjust sector size for the fact that the first three sectors are 128 bytes
  if (ss == 256)
    num_sectors += 2;

  return num_sectors;
}

unsigned long num_sectors_to_para(unsigned short num_sectors, unsigned short sector_size)
{
  unsigned long file_size = (num_sectors * sector_size);

  // Subtract bias for the first three sectors.
  if (sector_size > 128)
    file_size -= 384;

  return file_size >> 4;
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
  percomBlock[deviceSlot].num_sides = 0;
  percomBlock[deviceSlot].density = 0; // >128 bytes = MFM
  percomBlock[deviceSlot].sector_sizeM = (sectorSize == 256 ? 0x01 : 0x00);
  percomBlock[deviceSlot].sector_sizeL = (sectorSize == 256 ? 0x00 : 0x80);
  percomBlock[deviceSlot].drive_present = 255;
  percomBlock[deviceSlot].reserved1 = 0;
  percomBlock[deviceSlot].reserved2 = 0;
  percomBlock[deviceSlot].reserved3 = 0;

  if (numSectors == 1040) // 5/25" 1050 density
  {
    percomBlock[deviceSlot].sectors_per_trackM = 0;
    percomBlock[deviceSlot].sectors_per_trackL = 26;
    percomBlock[deviceSlot].density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 720 && sectorSize == 256) // 5.25" SS/DD
  {
    percomBlock[deviceSlot].density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 1440) // 5.25" DS/DD
  {
    percomBlock[deviceSlot].num_sides = 1;
    percomBlock[deviceSlot].density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 2880) // 5.25" DS/QD
  {
    percomBlock[deviceSlot].num_sides = 1;
    percomBlock[deviceSlot].num_tracks = 80;
    percomBlock[deviceSlot].density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 2002 && sectorSize == 128) // SS/SD 8"
  {
    percomBlock[deviceSlot].num_tracks = 77;
    percomBlock[deviceSlot].density = 0; // FM density
  }
  else if (numSectors == 2002 && sectorSize == 256) // SS/DD 8"
  {
    percomBlock[deviceSlot].num_tracks = 77;
    percomBlock[deviceSlot].density = 4; // MFM density
  }
  else if (numSectors == 4004 && sectorSize == 128) // DS/SD 8"
  {
    percomBlock[deviceSlot].num_tracks = 77;
    percomBlock[deviceSlot].density = 0; // FM density
  }
  else if (numSectors == 4004 && sectorSize == 256) // DS/DD 8"
  {
    percomBlock[deviceSlot].num_sides = 1;
    percomBlock[deviceSlot].num_tracks = 77;
    percomBlock[deviceSlot].density = 4; // MFM density
  }
  else if (numSectors == 5760) // 1.44MB 3.5" High Density
  {
    percomBlock[deviceSlot].num_sides = 1;
    percomBlock[deviceSlot].num_tracks = 80;
    percomBlock[deviceSlot].sectors_per_trackL = 36;
    percomBlock[deviceSlot].density = 8; // I think this is right.
  }
  else
  {
    // This is a custom size, one long track.
    percomBlock[deviceSlot].num_tracks = 1;
    percomBlock[deviceSlot].sectors_per_trackM = numSectors >> 8;
    percomBlock[deviceSlot].sectors_per_trackL = numSectors & 0xFF;
  }

#ifdef DEBUG_VERBOSE
  Debug_printf("Percom block dump for newly mounted device slot %d\n", deviceSlot);
  dump_percom_block(deviceSlot);
#endif
}

/**
   Read percom block
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
   Write percom block
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
  bool opened = tnfs_open(deviceSlot, options, false);

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
    num_sectors = para_to_num_sectors(num_para, num_para_hi, newss);
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
  byte hsd = HISPEED_INDEX;
  sio_to_computer((byte *)&hsd, 1, false);
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
    ss = sectorSize[deviceSlot];

    // Bias adjustment for 256 bytes
    if (ss == 256)
      offset -= 384;

    offset += 16; // skip 16 byte ATR Header
  }

  memset(sector, 0, 256); // clear buffer

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

#ifdef DEBUG
  Debug_printf("Read sector #%u\n", sectorNum);
#endif

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
    if (sectorNum < 4)
      ss = 128;
    else
      ss = sectorSize[deviceSlot];

    offset = sectorNum;
    offset *= ss;
    offset -= ss;

    // Bias adjustment for 256 bytes
    if (ss == 256)
      offset -= 384;

#ifdef DEBUG
    Debug_printf("Sector size: %d\n", ss);
#endif
    offset += 16; // ATR header

    tnfs_seek(deviceSlot, offset);
    tnfs_read(deviceSlot, ss);
    d = &sector[0];
    s = &tnfsPacket.data[3];
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

#ifdef DEBUG_VERBOSE
    Debug_print("Mounting / from ");
    Debug_println((char*)hostSlots.host[hostSlot]);
    for (int i = 0; i < 32; i++)
      Debug_printf("%02x ", hostSlots.host[hostSlot][i]);
    Debug_printf("\n\n");
    Debug_print("Req Packet: ");
    for (int i = 0; i < 10; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S */

    UDP.beginPacket(String(hostSlots.host[hostSlot]).c_str(), 16384);
    UDP.write(tnfsPacket.rawData, 10);
    UDP.endPacket();

#ifdef DEBUG_VERBOSE
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
#endif /* DEBUG_S */
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
#ifdef DEBUG_VERBOSE
          Debug_print("Successful, Session ID: ");
          Debug_print(tnfsPacket.session_idl, HEX);
          Debug_println(tnfsPacket.session_idh, HEX);
#endif /* DEBUG_S */
          // Persist the session ID.
          tnfsSessionIDs[hostSlot].session_idl = tnfsPacket.session_idl;
          tnfsSessionIDs[hostSlot].session_idh = tnfsPacket.session_idh;
          return true;
        }
        else
        {
          // Error
#ifdef DEBUG_VERBOSE
          Debug_print("Error #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S */
          return false;
        }
      }
    }
    // Otherwise we timed out.
#ifdef DEBUG_VERBOSE
    Debug_println("Timeout after 5000ms");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
  return false;
}

/**
   Open 'autorun.atr'
*/
bool tnfs_open(unsigned char deviceSlot, unsigned char options, bool create)
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

    tnfsPacket.data[c++] = (create == true ? 0x01 : 0x00); // Create flag
    tnfsPacket.data[c++] = 0xFF; // mode
    tnfsPacket.data[c++] = 0x01; //
    tnfsPacket.data[c++] = '/'; // Filename start

    for (int i = 0; i < strlen(mountPath); i++)
    {
      tnfsPacket.data[i + 5] = mountPath[i];
      c++;
    }

    tnfsPacket.data[c++] = 0x00;
    tnfsPacket.data[c++] = 0x00;
    tnfsPacket.data[c++] = 0x00;

#ifdef DEBUG_VERBOSE
    Debug_printf("Opening /%s\n", mountPath);
    Debug_println("");
    Debug_print("Req Packet: ");
    for (int i = 0; i < c + 4; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
#endif /* DEBUG_S */

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
#endif // DEBUG_S
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          tnfs_fds[deviceSlot] = tnfsPacket.data[1];
#ifdef DEBUG_VERBOSE
          Debug_print("Successful, file descriptor: #");
          Debug_println(tnfs_fds[deviceSlot], HEX);
#endif /* DEBUG_S */
          return true;
        }
        else
        {
          // unsuccessful
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("Failed\n");
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
#endif // DEBUG_S
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
          return true;
        }
        else
        {
          // unsuccessful
#ifdef DEBUG_VERBOSE
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
    // Otherwise, we timed out.
    retries++;
    tnfsPacket.retryCount--;
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("Failed\n");
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
#ifdef DEBUG_VERBOSE
          Debug_printf("Opened dir on slot #%d - fd = %02x\n", hostSlot, tnfs_dir_fds[hostSlot]);
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

#ifdef DEBUG_VERBOSE
    Debug_printf("TNFS Read next dir entry, slot #%d - fd %02x\n\n", hostSlot, tnfs_dir_fds[hostSlot]);
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
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
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

#ifdef DEBUG_VERBOSE
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
#endif /* DEBUG_S */
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
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

#ifdef DEBUG_VERBOSE
    Debug_print("Writing to File descriptor: ");
    Debug_println(tnfs_fds[deviceSlot]);
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S */

    UDP.beginPacket(hostSlots.host[deviceSlots.slot[deviceSlot].hostSlot], 16384);
    UDP.write(tnfsPacket.rawData, 4 + 3);
    UDP.write(sector, len);
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
#endif /* DEBUG_S */
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
#ifdef DEBUG_VERBOSE
          Debug_println("Successful.");
#endif /* DEBUG_S */
          return true;
        }
        else
        {
          // Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
}

/**
   TNFS Write blank ATR
*/
bool tnfs_write_blank_atr(unsigned char deviceSlot, unsigned short sectorSize, unsigned short numSectors)
{
  unsigned long num_para = num_sectors_to_para(numSectors, sectorSize);
  unsigned long offset;

  // Write header
  atrHeader.magic1 = 0x96;
  atrHeader.magic2 = 0x02;
  atrHeader.filesizeH = num_para & 0xFF;
  atrHeader.filesizeL = (num_para & 0xFF00) >> 8;
  atrHeader.filesizeHH = (num_para & 0xFF0000) >> 16;
  atrHeader.secsizeH = sectorSize & 0xFF;
  atrHeader.secsizeL = sectorSize >> 8;

#ifdef DEBUG
  Debug_printf("TNFS: Write header\n");
#endif
  memcpy(sector, atrHeader.rawData, sizeof(atrHeader.rawData));
  tnfs_write(deviceSlot, sizeof(atrHeader.rawData));
  offset += sizeof(atrHeader.rawData);

  // Write first three 128 byte sectors
  memset(sector, 0x00, sizeof(sector));

#ifdef DEBUG
  Debug_printf("TNFS: Write first three sectors\n");
#endif

  for (unsigned char i = 0; i < 3; i++)
  {
    tnfs_write(deviceSlot, 128);
    offset += 128;
    numSectors--;
  }

#ifdef DEBUG
  Debug_printf("TNFS: Sparse Write the rest.\n");
#endif
  // Write the rest of the sectors via sparse seek
  offset += (numSectors * sectorSize) - sectorSize;
  tnfs_seek(deviceSlot, offset);
  tnfs_write(deviceSlot, sectorSize);
  return true; //fixme
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

#ifdef DEBUG_VERBOSE
    Debug_print("Reading from File descriptor: ");
    Debug_println(tnfs_fds[deviceSlot]);
    Debug_print("Req Packet: ");
    for (int i = 0; i < 7; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S */

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
#endif /* DEBUG_S */
        if (tnfsPacket.data[0] == 0x00)
        {
          // Successful
#ifdef DEBUG_VERBOSE
          Debug_println("Successful.");
#endif /* DEBUG_S */
          return true;
        }
        else
        {
          // Error
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    if (retries < 5)
      Debug_printf("Retrying...\n");
#endif /* DEBUG_S */
    retries++;
    tnfsPacket.retryCount--;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
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
#endif /* DEBUG */
#ifdef DEBUG_VERBOSE
    Debug_print("Req packet: ");
    for (int i = 0; i < 10; i++)
    {
      Debug_print(tnfsPacket.rawData[i], HEX);
      Debug_print(" ");
    }
    Debug_println("");
#endif /* DEBUG_S*/

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
#ifdef DEBUG_VERBOSE
          Debug_println("Successful.");
#endif /* DEBUG_S */
          return true;
        }
        else
        {
          // Error.
#ifdef DEBUG
          Debug_print("Error code #");
          Debug_println(tnfsPacket.data[0], HEX);
#endif /* DEBUG_S*/
          return false;
        }
      }
    }
#ifdef DEBUG
    Debug_println("Timeout after 5000ms.");
    if (retries < 5)
      Debug_printf("Retrying...\n");
#endif /* DEBUG_S */
    tnfsPacket.retryCount--;
    retries++;
  }
#ifdef DEBUG
  Debug_printf("Failed.\n");
#endif
  return false;
}

void sio_wait()
{
  SIO_UART.read(); // Toss it for now
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
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_INT, HIGH);
  pinMode(PIN_PROC, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_PROC, HIGH);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
#ifdef ESP8266
  pinMode(PIN_LED, INPUT);
  digitalWrite(PIN_LED, HIGH); // off
#elif defined(ESP32)
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif

#ifdef DEBUG_N
  /* Get WiFi started, but don't wait for it otherwise SIO
     powered FujiNet fails to boot
  */
  WiFi.begin(DEBUG_SSID, DEBUG_PASSWORD);
#endif

  // Set up serial
  SIO_UART.begin(STANDARD_BAUDRATE);
  SIO_UART.setTimeout(SERIAL_TIMEOUT);
#ifdef ESP8266
  SIO_UART.swap();
#endif

  // Set up SIO command function pointers
  for (int i = 0; i < 256; i++)
    cmdPtr[i] = sio_wait;

  cmdPtr['P'] = sio_write;
  cmdPtr['W'] = sio_write;
  cmdPtr['R'] = sio_read;
  cmdPtr['S'] = sio_status;
  cmdPtr['!'] = sio_format;
  cmdPtr['"'] = sio_format;
  cmdPtr[0x3F] = sio_high_speed;
  cmdPtr[0x4E] = sio_read_percom_block;
  cmdPtr[0x4F] = sio_write_percom_block;
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
  cmdPtr[0xE7] = sio_new_disk;
  cmdPtr[0xE6] = sio_http_open;
  cmdPtr[0xE5] = sio_http_get;
  cmdPtr[0xE4] = sio_http_close;
  cmdPtr[0xE3] = sio_set_base_url;
  
  // Go ahead and flush anything out of the serial port
  sio_flush();
}

void loop()
{
  int a;
  if (digitalRead(PIN_CMD) == LOW)
  {
    sio_led(true);
    memset(cmdFrame.cmdFrameData, 0, 5); // clear cmd frame.

#ifdef ESP8266
    delayMicroseconds(DELAY_T0); // computer is waiting for us to notice.
#endif

    // read cmd frame
    SIO_UART.readBytes(cmdFrame.cmdFrameData, 5);
#ifdef DEBUG
    Debug_printf("CF: %02x %02x %02x %02x %02x\n", cmdFrame.devic, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2, cmdFrame.cksum);
#endif

    byte ck = sio_checksum(cmdFrame.cmdFrameData, 4);
    if (ck == cmdFrame.cksum)
    {
#ifdef ESP8266
      delayMicroseconds(DELAY_T1);
#endif
      // Wait for CMD line to raise again
      while (digitalRead(PIN_CMD) == LOW) yield();
#ifdef ESP8266
      delayMicroseconds(DELAY_T2);
#endif
      if (sio_valid_device_id())
      {
        if (cmdPtr[cmdFrame.comnd] == sio_wait)
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
      }
    }
    else
    {
      command_frame_counter++;
      if (COMMAND_FRAME_SPEED_CHANGE_THRESHOLD == command_frame_counter)
      {
        command_frame_counter = 0;
        if (hispeed)
        {
#ifdef DEBUG
          Debug_printf("Switching to %d baud...\n", STANDARD_BAUDRATE);
#endif
          SIO_UART.updateBaudRate(STANDARD_BAUDRATE);
          hispeed = false;
        }
        else
        {
#ifdef DEBUG
          Debug_printf("Switching to %d baud...\n", HISPEED_BAUDRATE);
#endif
          SIO_UART.updateBaudRate(HISPEED_BAUDRATE);
          hispeed = true;
        }
      }
    }
    sio_led(false);
  }
  else
  {
    sio_led(false);
    a = SIO_UART.available();
    if (a)
      while (SIO_UART.available())
        SIO_UART.read(); // dump it.
  }
}
