#include "disk.h"

#include "fnSystem.h"
#include "fnFsTNFS.h"
#include "fnFsSD.h"
#include "led.h"

FileSystemTNFS tserver;

iwmDisk::~iwmDisk()
{
}

void iwmDisk::init()
{
  open_tnfs_image();
  //open_image("/prodos8abbrev.po");//("/STABLE.32MB.po");
  if (d.sdf != nullptr)
    Debug_printf("\r\nfile open good");
  else
    Debug_printf("\r\nImage open error!");
  Debug_printf("\r\nDemo TNFS file open complete - remember to remove this code");
}

bool iwmDisk::open_tnfs_image()
{
  Debug_printf("\r\nmounting server");
  tserver.start("159.203.160.80"); //"atari-apps.irata.online");
  Debug_printf("\r\nopening file");
  d.sdf = tserver.file_open("/test.hdv", "rb");

  Debug_printf(("\r\nTesting file "));
  // d.sdf.printName();
  if (d.sdf == nullptr) // .isOpen()||!d.sdf.isFile())
  {
    Debug_printf(("\r\nFile must exist, be open and be a regular file before checking for valid image type!"));
    return false;
  }

  long s = tserver.filesize(d.sdf);

  if ((s != ((s >> 9) << 9)) || (s == 0) || (s == -1))
  {
    Debug_printf(("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Debug_printf(("\r\nThis means its size must be an exact multiple of 512!"));
    return false;
  }

  Debug_printf(("\r\nFile good!"));
  d.blocks = tserver.filesize(d.sdf) >> 9;

  return true;
}
// TODO: Allow image files with headers, too
// TODO: Respect read-only bit in header
bool iwmDisk::open_image(std::string filename)
{
  // d.sdf = sdcard.open(filename, O_RDWR);
  Debug_printf("\r\nright before file open call");
  d.sdf = fnSDFAT.file_open(filename.c_str(), "rb");
  Debug_printf(("\r\nTesting file "));
  // d.sdf.printName();
  if (d.sdf == nullptr) // .isOpen()||!d.sdf.isFile())
  {
    Debug_printf(("\r\nFile must exist, be open and be a regular file before checking for valid image type!"));
    return false;
  }

  long s = fnSDFAT.filesize(d.sdf);
  if ((s != ((s >> 9) << 9)) || (s == 0) || (s == -1))
  {
    Debug_printf(("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Debug_printf(("\r\nThis means its size must be an exact multiple of 512!"));
    return false;
  }

  Debug_printf(("\r\nFile good!"));
  d.blocks = fnSDFAT.filesize(d.sdf) >> 9;

  return true;
}

//*****************************************************************************
// Function: encode_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command packet. The reply
// includes following:
// data byte 1 is general info.
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Size determined from image file.
//*****************************************************************************
void iwmDisk::encode_status_reply_packet()
{

  uint8_t checksum = 0;
  uint8_t data[4];

  // Build the contents of the packet
  // Info byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Currently open (char devices only)
  data[0] = 0b11111000;
  // Disk size
  data[1] = d.blocks & 0xff;
  data[2] = (d.blocks >> 8) & 0xff;
  data[3] = (d.blocks >> 16) & 0xff;

  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;        // PBEGIN - start byte
  packet_buffer[7] = 0x80;        // DEST - dest id - host
  packet_buffer[8] = id(); //d.device_id; // SRC - source id - us
  packet_buffer[9] = 0x81;        // TYPE -status
  packet_buffer[10] = 0x80;       // AUX
  packet_buffer[11] = 0x80;       // STAT - data status
  packet_buffer[12] = 0x84;       // ODDCNT - 4 data bytes
  packet_buffer[13] = 0x80;       // GRP7CNT
  // 4 odd bytes
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
  packet_buffer[15] = data[0] | 0x80;                                                                                               // data 1
  packet_buffer[16] = data[1] | 0x80;                                                                                               // data 2
  packet_buffer[17] = data[2] | 0x80;                                                                                               // data 3
  packet_buffer[18] = data[3] | 0x80;                                                                                               // data 4

  for (int i = 0; i < 4; i++)
  { // calc the data bytes checksum
    checksum ^= data[i];
  }
  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[19] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[20] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[21] = 0xc8; // PEND
  packet_buffer[22] = 0x00; // end of packet in buffer
}

//*****************************************************************************
// Function: encode_long_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the extended status command packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Size determined from image file.
//*****************************************************************************
void iwmDisk::encode_extended_status_reply_packet()
{
  uint8_t checksum = 0;

  uint8_t data[5];

  // Build the contents of the packet
  // Info byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Currently open (char devices only)
  data[0] = 0b11111000;
  // Disk size
  data[1] = d.blocks & 0xff;
  data[2] = (d.blocks >> 8) & 0xff;
  data[3] = (d.blocks >> 16) & 0xff;
  data[4] = (d.blocks >> 24) & 0xff;

  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;        // PBEGIN - start byte
  packet_buffer[7] = 0x80;        // DEST - dest id - host
  packet_buffer[8] = id(); // d.device_id; // SRC - source id - us
  packet_buffer[9] = 0xC1;        // TYPE - extended status
  packet_buffer[10] = 0x80;       // AUX
  packet_buffer[11] = 0x80;       // STAT - data status
  packet_buffer[12] = 0x85;       // ODDCNT - 5 data bytes
  packet_buffer[13] = 0x80;       // GRP7CNT
  // 5 odd bytes
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08) | ((data[4] >> 5) & 0x04); // odd msb
  packet_buffer[15] = data[0] | 0x80;                                                                                                                         // data 1
  packet_buffer[16] = data[1] | 0x80;                                                                                                                         // data 2
  packet_buffer[17] = data[2] | 0x80;                                                                                                                         // data 3
  packet_buffer[18] = data[3] | 0x80;                                                                                                                         // data 4
  packet_buffer[19] = data[4] | 0x80;                                                                                                                         // data 5

  for (int i = 0; i < 5; i++)
  { // calc the data bytes checksum
    checksum ^= data[i];
  }
  // calc the data bytes checksum
  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[20] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[21] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[22] = 0xc8; // PEND
  packet_buffer[23] = 0x00; // end of packet in buffer
}

//*****************************************************************************
// Function: encode_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void iwmDisk::encode_status_dib_reply_packet()
{
  int grpbyte, grpcount, i;
  int grpnum, oddnum;
  uint8_t checksum = 0, grpmsb;
  uint8_t group_buffer[7];
  uint8_t data[25];
  // data buffer=25: 3 x Grp7 + 4 odds
  grpnum = 3;
  oddnum = 4;

  //* write data buffer first (25 bytes) 3 grp7 + 4 odds
  data[0] = 0xf8; // general status - f8
  // number of blocks =0x00ffff = 65525 or 32mb
  data[1] = d.blocks & 0xff;         // block size 1
  data[2] = (d.blocks >> 8) & 0xff;  // block size 2
  data[3] = (d.blocks >> 16) & 0xff; // block size 3
  data[4] = 0x07;                    // ID string length - 11 chars
  data[5] = 'F';
  data[6] = 'U';
  data[7] = 'J';
  data[8] = 'I';
  data[9] = 'N';
  data[10] = 'E';
  data[11] = 'T';
  data[12] = ' ';
  data[13] = ' ';
  data[14] = ' ';
  data[15] = ' ';
  data[16] = ' ';
  data[17] = ' ';
  data[18] = ' ';
  data[19] = ' ';
  data[20] = ' ';  // ID string (16 chars total)
  data[21] = 0x02; // Device type    - 0x02  harddisk
  data[22] = 0x0a; // Device Subtype - 0x0a
  data[23] = 0x01; // Firmware version 2 bytes
  data[24] = 0x0f; //

  // print_packet ((uint8_t*) data,packet_length()); // debug
  // Debug_print(("\nData loaded"));
  // Calculate checksum of sector bytes before we destroy them
  for (int count = 0; count < 25; count++) // xor all the data bytes
    checksum = checksum ^ data[count];

  // Start assembling the packet at the rear and work
  // your way to the front so we don't overwrite data
  // we haven't encoded yet

  // grps of 7
  for (grpcount = grpnum - 1; grpcount >= 0; grpcount--) // 3
  {
    for (i = 0; i < 8; i++)
    {
      group_buffer[i] = data[i + oddnum + (grpcount * 7)];
    }
    // add group msb byte
    grpmsb = 0;
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      grpmsb = grpmsb | ((group_buffer[grpbyte] >> (grpbyte + 1)) & (0x80 >> (grpbyte + 1)));
    packet_buffer[(14 + oddnum + 1) + (grpcount * 8)] = grpmsb | 0x80; // set msb to one

    // now add the group data bytes bits 6-0
    for (grpbyte = 0; grpbyte < 7; grpbyte++)
      packet_buffer[(14 + oddnum + 2) + (grpcount * 8) + grpbyte] = group_buffer[grpbyte] | 0x80;
  }

  // odd byte
  packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
  packet_buffer[15] = data[0] | 0x80;
  packet_buffer[16] = data[1] | 0x80;
  packet_buffer[17] = data[2] | 0x80;
  packet_buffer[18] = data[3] | 0x80;
  ;

  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;
  packet_buffer[6] = 0xc3;        // PBEGIN - start byte
  packet_buffer[7] = 0x80;        // DEST - dest id - host
  packet_buffer[8] = id(); // d.device_id; // SRC - source id - us
  packet_buffer[9] = 0x81;        // TYPE -status
  packet_buffer[10] = 0x80;       // AUX
  packet_buffer[11] = 0x80;       // STAT - data status
  packet_buffer[12] = 0x84;       // ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83;       // GRP7CNT - 3 grps of 7

  for (int count = 7; count < 14; count++) // xor the packet header bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[43] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[44] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[45] = 0xc8; // PEND
  packet_buffer[46] = 0x00; // end of packet in buffer
}

//*****************************************************************************
// Function: encode_long_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void iwmDisk::encode_extended_status_dib_reply_packet()
{
  uint8_t checksum = 0;

  packet_buffer[0] = 0xff; // sync bytes
  packet_buffer[1] = 0x3f;
  packet_buffer[2] = 0xcf;
  packet_buffer[3] = 0xf3;
  packet_buffer[4] = 0xfc;
  packet_buffer[5] = 0xff;

  packet_buffer[6] = 0xc3;        // PBEGIN - start byte
  packet_buffer[7] = 0x80;        // DEST - dest id - host
  packet_buffer[8] = id(); //d.device_id; // SRC - source id - us
  packet_buffer[9] = 0x81;        // TYPE -status
  packet_buffer[10] = 0x80;       // AUX
  packet_buffer[11] = 0x83;       // STAT - data status
  packet_buffer[12] = 0x80;       // ODDCNT - 4 data bytes
  packet_buffer[13] = 0x83;       // GRP7CNT - 3 grps of 7
  packet_buffer[14] = 0xf0;       // grp1 msb
  packet_buffer[15] = 0xf8;       // general status - f8
  // number of blocks =0x00ffff = 65525 or 32mb
  packet_buffer[16] = d.blocks & 0xff;                  // block size 1
  packet_buffer[17] = (d.blocks >> 8) & 0xff;           // block size 2
  packet_buffer[18] = ((d.blocks >> 16) & 0xff) | 0x80; // block size 3 - why is the high bit set?
  packet_buffer[19] = ((d.blocks >> 24) & 0xff) | 0x80; // block size 3 - why is the high bit set?
  packet_buffer[20] = 0x8d;                             // ID string length - 13 chars
  packet_buffer[21] = 'S';
  packet_buffer[22] = 'm';  // ID string (16 chars total)
  packet_buffer[23] = 0x80; // grp2 msb
  packet_buffer[24] = 'a';
  packet_buffer[25] = 'r';
  packet_buffer[26] = 't';
  packet_buffer[27] = 'p';
  packet_buffer[28] = 'o';
  packet_buffer[29] = 'r';
  packet_buffer[30] = 't';
  packet_buffer[31] = 0x80; // grp3 msb
  packet_buffer[32] = ' ';
  packet_buffer[33] = 'S';
  packet_buffer[34] = 'D';
  packet_buffer[35] = ' ';
  packet_buffer[36] = ' ';
  packet_buffer[37] = ' ';
  packet_buffer[38] = ' ';
  packet_buffer[39] = 0x80; // odd msb
  packet_buffer[40] = 0x02; // Device type    - 0x02  harddisk
  packet_buffer[41] = 0x00; // Device Subtype - 0x20
  packet_buffer[42] = 0x01; // Firmware version 2 bytes
  packet_buffer[43] = 0x0f;
  packet_buffer[44] = 0x90; //

  for (int count = 7; count < 45; count++) // xor the packet bytes
    checksum = checksum ^ packet_buffer[count];
  packet_buffer[45] = checksum | 0xaa;      // 1 c6 1 c4 1 c2 1 c0
  packet_buffer[46] = checksum >> 1 | 0xaa; // 1 c7 1 c5 1 c3 1 c1

  packet_buffer[47] = 0xc8; // PEND
  packet_buffer[48] = 0x00; // end of packet in buffer
}

void iwmDisk::process()
{
  fnLedManager.set(LED_BUS, true);
  switch (packet_buffer[14])
  {
  case 0x80: // status
      Debug_printf("\r\nhandling status command");
    iwm_status();
    break;
  case 0x81: // read block
    Debug_printf("\r\nhandling read block command");
    iwm_readblock();
    break;
  case 0x82: // write block
    Debug_printf("\r\nhandling write block command");
    iwm_writeblock();
    break;
  case 0x83: // format
    break;
  case 0x84: // control
    break;
  // case 0x85: // init
  //   break;
  case 0x86: // open
    break;
  case 0x87: // close
    break;
  case 0x88: // read
    break;
  case 0x89: // write
    break;
  } // switch (cmd)
  fnLedManager.set(LED_BUS, false);
}

unsigned long int last_block_num=0xFFFFFFFF;

void iwmDisk::iwm_readblock()
{
  uint8_t LBH, LBL, LBN, LBT;
  unsigned long int block_num;
  size_t sdstato;
  uint8_t source;

  source = packet_buffer[6];
  Debug_printf("\r\nDrive %02x ", source);

  LBH = packet_buffer[16]; // high order bits
  LBT = packet_buffer[21]; // block number high
  LBL = packet_buffer[20]; // block number middle
  LBN = packet_buffer[19]; // block number low
  block_num = (LBN & 0x7f) | (((unsigned short)LBH << 3) & 0x80);
  // block num second byte
  // print_packet ((unsigned char*) packet_buffer,packet_length());
  // Added (unsigned short) cast to ensure calculated block is not underflowing.
  block_num = block_num + (((LBL & 0x7f) | (((unsigned short)LBH << 4) & 0x80)) << 8);
  block_num = block_num + (((LBT & 0x7f) | (((unsigned short)LBH << 5) & 0x80)) << 16);
  Debug_printf("Read block %04x", block_num);

  if (block_num != last_block_num + 1) // example optimization, only do seek if not reading next block -tschak
  {
    Debug_printf("\r\n");
    if (fseek(d.sdf, (block_num * 512), SEEK_SET))
    {
      Debug_printf("\r\nRead seek err! block #%02x", block_num);
      if (d.sdf != nullptr)
      {
        Debug_printf("\r\nPartition file is open!");
      }
      else
      {
        Debug_printf("\r\nPartition file is closed!");
      }
      return;
    }
  }

  sdstato = fread((unsigned char *)packet_buffer, 1, 512, d.sdf); // Reading block from SD Card
  if (sdstato != 512)
  {
    Debug_printf("\r\nFile Read err: %d bytes", sdstato);
    return;
  }
  encode_data_packet(source);
  Debug_printf("\r\nsending block packet ...");
  if (!IWM.iwm_send_packet((unsigned char *)packet_buffer))
    last_block_num = block_num;
}

void iwmDisk::iwm_writeblock()
{
  uint8_t source = packet_buffer[6];
  Debug_printf("\r\nDrive %02x ", source);
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  unsigned long int block_num = (packet_buffer[19] & 0x7f) | (((unsigned short)packet_buffer[16] << 3) & 0x80);
  // block num second byte
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  block_num = block_num + (((packet_buffer[20] & 0x7f) | (((unsigned short)packet_buffer[16] << 4) & 0x80)) * 256);
  Debug_printf("Write block %04x", block_num);
  //get write data packet, keep trying until no timeout
  if (IWM.iwm_read_packet_timeout(100, (unsigned char *)packet_buffer))
  {
#ifdef DEBUG
    print_packet();
#endif
    Debug_printf("\r\nTIMEOUT in read packet!");
    return;
  }

#ifdef DEBUG
  print_packet();
#endif
  // partition number indicates which 32mb block we access on the CF
  // TODO: replace this with a lookup to get file object from partition number
  // block_num = block_num + (((partition + initPartition) % 4) * 65536);
  int status = decode_data_packet();
  if (status == 0)
  { //ok
    //write block to CF card
    //Serial.print(F("\r\nWrite Bl. n.r: "));
    //Serial.print(block_num);
    if (block_num != last_block_num + 1) // example optimization, only do seek if not writing next block -tschak
    {
      Debug_printf("\r\n");
      if (fseek(d.sdf, (block_num * 512), SEEK_SET))
      {
        Debug_printf("\r\nRead seek err! block #%02x", block_num);
        if (d.sdf != nullptr)
        {
          Debug_printf("\r\nPartition file is open!");
        }
        else
        {
          Debug_printf("\r\nPartition file is closed!");
        }
        //return;
      }
    }
    size_t sdstato = fwrite((unsigned char *)packet_buffer, 1, 512, d.sdf);
    if (sdstato != 512)
    {
      Debug_printf("\r\nFile Write err: %d bytes", sdstato);
      if (sdstato == 0)
        status = 0x2B; // write protected todo: we should probably have a read-only flag that gets set and tested up top
      else
        status = 0x27; // 6;
      //return;
    }
    //now return status code to host
    encode_write_status_packet(source, status);
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
#ifdef DEBUG
    print_packet();
#endif
    //Serial.print(F("\r\nSent status Packet Data\r\n") );
    //print_packet ((unsigned char*) sector_buffer,512);

    //print_packet ((unsigned char*) packet_buffer,packet_length());
    last_block_num = block_num;
  }
}

void iwmDisk::iwm_read()
{
}

void iwmDisk::iwm_write(bool verify)
{
}

// void iwm_format();

void iwmDisk::iwm_status() // override;
{
  // uint8_t source = packet_buffer[6];

  if (d.sdf != nullptr)
  { 
    uint8_t status_code = (packet_buffer[19] & 0x7f); // | (((unsigned short)packet_buffer[16] << 3) & 0x80);
    //Serial.print(F("\r\nStatus code: "));
    //Serial.print(status_code);
    //print_packet ((unsigned char*) packet_buffer, packet_length());
    //Serial.print(F("\r\nHere's the decoded status packet because frig doing it by hand!"));
    //decode_data_packet();
    //print_packet((unsigned char*) packet_buffer, 9); //Standard SmartPort command is 9 bytes
    //if (status_code |= 0x00) { // TEST
    //  Serial.print(F("\r\nStatus not zero!! ********"));
    //  print_packet ((unsigned char*) packet_buffer,packet_length());}
    if (status_code == 0x03)
    { // if statcode=3, then status with device info block
      Debug_printf("\r\n******** Sending DIB! ********");
      encode_status_dib_reply_packet();
      //print_packet ((unsigned char*) packet_buffer,packet_length());
      fnSystem.delay(50);
    }
    else
    { // else just return device status
      /*
                  Serial.print(F("\r\n-------- Sending status! --------"));
                  Serial.print(F("\r\nSource: "));
                  Serial.print(source,HEX);
                  Serial.print(F(" Partition ID: "));
                  Serial.print(devices[(partition + initPartition) % NUM_PARTITIONS].device_id, HEX);
                  Serial.print(F(" Status code: "));
                  Serial.print(status_code, HEX);
                  */
      Debug_printf("\r\nSending Status");
      encode_status_reply_packet();
    }
   IWM.iwm_send_packet((unsigned char *)packet_buffer);
   print_packet();
  }
}
// void derive_percom_block(uint16_t numSectors);
// void iwm_read_percom_block();
// void iwm_write_percom_block();
// void dump_percom_block();

void iwmDisk::shutdown()
{
}

iwmDisk::iwmDisk()
{
}

mediatype_t iwmDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
  return disk_type;
}

void iwmDisk::unmount()
{
}

bool iwmDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
  return false;
}
