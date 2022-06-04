#ifdef BUILD_APPLE
#include "disk.h"

#include "fnSystem.h"
#include "fnFsTNFS.h"
#include "fnFsSD.h"
#include "led.h"
#include "fuji.h"

#define LOCAL_TNFS

FileSystemTNFS tserver;

iwmDisk::~iwmDisk()
{
}
/* 
void iwmDisk::init()
{
  open_tnfs_image();
  //open_image("/autorun.po");//("/STABLE.32MB.po");
  if (_disk->status())
  {
    Debug_printf("\r\nfile open good");
  }
  else
  {
    Debug_printf("\r\nImage open error!");
  }
  Debug_printf("\r\nDemo TNFS file open complete - remember to remove this code");
}
 */
/* 
bool iwmDisk::open_tnfs_image()
{
#ifdef LOCAL_TNFS
  Debug_printf("\r\nmounting server");
  tserver.start("192.168.1.181"); //"atari-apps.irata.online");
  Debug_printf("\r\nopening file");
  _disk->fileptr() = tserver.file_open("/autorun.po", "rb+");
  // _disk->fileptr() = tserver.file_open("/prodos8abbrev.po", "rb+");
  // _disk->fileptr() = tserver.file_open("/prodos16mb.po", "rb");
#else
  Debug_printf("\r\nmounting server");
  tserver.start("159.203.160.80"); //"atari-apps.irata.online");
  Debug_printf("\r\nopening file");
  _disk->fileptr() = tserver.file_open("/test.hdv", "rb");
#endif

  Debug_printf(("\r\nTesting file "));
  // _disk->fileptr().printName();
  if (_disk->fileptr() == nullptr) // .isOpen()||!_disk->fileptr().isFile())
  {
    Debug_printf(("\r\nFile must exist, be open and be a regular file before checking for valid image type!"));
    return false;
  }

  long s = tserver.filesize(_disk->fileptr());

  if ((s != ((s >> 9) << 9)) || (s == 0) || (s == -1))
  {
    Debug_printf(("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Debug_printf(("\r\nThis means its size must be an exact multiple of 512!"));
    return false;
  }

  Debug_printf(("\r\nFile good!"));
  _disk->num_blocks = tserver.filesize(_disk->fileptr()) >> 9;

  return true;
}
 */
// TODO: Allow image files with headers, too
// TODO: Respect read-only bit in header
/* 
bool iwmDisk::open_image(std::string filename)
{
  // _disk->fileptr() = sdcard.open(filename, O_RDWR);
  Debug_printf("\r\nright before file open call");
  _disk->fileptr() = fnSDFAT.file_open(filename.c_str(), "rb");
  Debug_printf(("\r\nTesting file "));
  // _disk->fileptr().printName();
  if (_disk->fileptr() == nullptr) // .isOpen()||!_disk->fileptr().isFile())
  {
    Debug_printf(("\r\nFile must exist, be open and be a regular file before checking for valid image type!"));
    return false;
  }

  long s = fnSDFAT.filesize(_disk->fileptr());
  if ((s != ((s >> 9) << 9)) || (s == 0) || (s == -1))
  {
    Debug_printf(("\r\nFile must be an unadorned ProDOS order image with no header!"));
    Debug_printf(("\r\nThis means its size must be an exact multiple of 512!"));
    return false;
  }

  Debug_printf(("\r\nFile good!"));
  _disk->num_blocks = fnSDFAT.filesize(_disk->fileptr()) >> 9;

  return true;
}
 */

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
  data[0] = 0b11101000;
  data[1] = data[2] = data[3] = 0;
  if (_disk != nullptr)
  {
    data[0] |= (1 << 4);
    // Disk size
    data[1] = _disk->num_blocks & 0xff;
    data[2] = (_disk->num_blocks >> 8) & 0xff;
    data[3] = (_disk->num_blocks >> 16) & 0xff;
}
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
  packet_buffer[20] = (checksum >> 1) | 0xaa; // 1 c7 1 c5 1 c3 1 c1

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
  data[0] = 0b11101000;
  // Build the contents of the packet
  // Info byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected (block devices only)
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Currently open (char devices only)
  data[1] = data[2] = data[3] = data[4] = 0;
  if (_disk!=nullptr)
  {
    data[0] |= (1 << 4);
    // Disk size
    data[1] = _disk->num_blocks & 0xff;
    data[2] = (_disk->num_blocks >> 8) & 0xff;
    data[3] = (_disk->num_blocks >> 16) & 0xff;
    data[4] = (_disk->num_blocks >> 24) & 0xff;
  
  }
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
void iwmDisk::encode_status_dib_reply_packet() // to do - abstract this out with passsed parameters
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
  // General Status byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected (block devices only)
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Currently open (char devices only)
  data[0] = 0b11101000;
  data[1] = 0;
  data[2] = 0;
  data[3] = 0;
  if (_disk != nullptr)
  {
    data[0] |= (1 << 4);
    data[1] = (_disk->num_blocks) & 0xff;         // block size 1
    data[2] = (_disk->num_blocks >> 8) & 0xff;  // block size 2
    data[3] = (_disk->num_blocks >> 16) & 0xff; // block size 3
    Debug_printf("\r\nDIB number of blocks %d", _disk->num_blocks);
    //Debug_printf("\r\n%02x %02x %02x %02x", data[0], data[1], data[2], data[3]);
  }
  Debug_printf("\r\n%02x %02x %02x %02x", data[0], data[1], data[2], data[3]); // this debug is required to make it work
  // ALERT!!!!!! The above debug is somehow required to make the assignment of data[0..3] above stick.
  // otherwise, data[0..3]=0. Have no idea why!?!?!?!?!??!?!?!?!?!?!?!?!??!
  data[4] = 0x0E; // ID string length - 14 chars
  data[5] = 'F';
  data[6] = 'U';
  data[7] = 'J';
  data[8] = 'I';
  data[9] = 'N';
  data[10] = 'E';
  data[11] = 'T';
  data[12] = '_';
  data[13] = 'D';
  data[14] = 'I';
  data[15] = 'S';
  data[16] = 'K';
  data[17] = '_';
  data[18] = disk_num; //'1';
  data[19] = ' ';
  data[20] = ' ';  // ID string (16 chars total)
  data[21] = 0x02; // Device type    - 0x02  harddisk
  data[22] = 0x0a; // Device Subtype - 0x0a
  data[23] = 0x01; // Firmware version 2 bytes
  data[24] = 0x0f; //

  // print_packet ((uint8_t*) data,get_packet_length()); // debug
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

    //Debug_printf("\r\n%02x %02x %02x %02x", data[0], data[1], data[2], data[3]);
    // odd byte
    packet_buffer[14] = 0x80 | ((data[0] >> 1) & 0x40) | ((data[1] >> 2) & 0x20) | ((data[2] >> 3) & 0x10) | ((data[3] >> 4) & 0x08); // odd msb
    packet_buffer[15] = data[0] | 0x80;
    packet_buffer[16] = data[1] | 0x80;
    packet_buffer[17] = data[2] | 0x80;
    packet_buffer[18] = data[3] | 0x80;
    ;

    Debug_printf("\r\npacket buffer 14: %02x", packet_buffer[14]);

    packet_buffer[0] = 0xff; // sync bytes
    packet_buffer[1] = 0x3f;
    packet_buffer[2] = 0xcf;
    packet_buffer[3] = 0xf3;
    packet_buffer[4] = 0xfc;
    packet_buffer[5] = 0xff;
    packet_buffer[6] = 0xc3;  // PBEGIN - start byte
    packet_buffer[7] = 0x80;  // DEST - dest id - host
    packet_buffer[8] = id();  // d.device_id; // SRC - source id - us
    packet_buffer[9] = 0x81;  // TYPE -status
    packet_buffer[10] = 0x80; // AUX
    packet_buffer[11] = 0x80; // STAT - data status
    packet_buffer[12] = 0x84; // ODDCNT - 4 data bytes
    packet_buffer[13] = 0x83; // GRP7CNT - 3 grps of 7

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
  if (_disk != nullptr)
  {
    packet_buffer[15] = 0b11101000 | (1 << 4); // general status - f8
    // number of blocks =0x00ffff = 65525 or 32mb
    packet_buffer[16] = _disk->num_blocks & 0xff;                  // block size 1
    packet_buffer[17] = (_disk->num_blocks >> 8) & 0xff;           // block size 2
    packet_buffer[18] = ((_disk->num_blocks >> 16) & 0xff) | 0x80; // block size 3 - why is the high bit set?
    packet_buffer[19] = ((_disk->num_blocks >> 24) & 0xff) | 0x80; // block size 3 - why is the high bit set?
  }
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

void iwmDisk::process(cmdPacket_t cmd)
{
  uint8_t status_code;
  fnLedManager.set(LED_BUS, true);
  switch (cmd.command)
  {
  case 0x80: // status
    Debug_printf("\r\nhandling status command");
    status_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    if (disk_num == '0' && status_code > 0x05) // max regular status code is 0x05 to UniDisk
      theFuji.FujiStatus(cmd);
    else  
      iwm_status(cmd);
    break;
  case 0x81: // read block
    Debug_printf("\r\nhandling read block command");
    iwm_readblock(cmd);
    break;
  case 0x82: // write block
    Debug_printf("\r\nhandling write block command");
    iwm_writeblock(cmd);
    break;
  case 0x83: // format
    iwm_return_badcmd(cmd);
    break;
  case 0x84: // control
    status_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    if (disk_num == '0' && status_code > 0x0A) // max regular control code is 0x0A to 3.5" disk
      theFuji.FujiControl(cmd);
    else  
      iwm_return_badcmd(cmd);
    break;
  case 0x86: // open
    iwm_return_badcmd(cmd);
    break;
  case 0x87: // close
    iwm_return_badcmd(cmd);
    break;
  case 0x88: // read
    iwm_return_badcmd(cmd);
    break;
  case 0x89: // write
    iwm_return_badcmd(cmd);
    break;
  default:
    iwm_return_badcmd(cmd);
  } // switch (cmd)
  fnLedManager.set(LED_BUS, false);
}

unsigned long int last_block_num=0xFFFFFFFF;

void iwmDisk::iwm_readblock(cmdPacket_t cmd)
{
  uint8_t LBH, LBL, LBN, LBT;
  unsigned long int block_num;
  size_t sdstato;
  uint8_t source;

  source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];
  Debug_printf("\r\nDrive %02x ", source);

  if (!(_disk != nullptr))
  {
    Debug_printf(" - ERROR - No image mounted");
    encode_error_reply_packet(SP_ERR_OFFLINE);
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
    return;
  }

  LBH = cmd.grp7msb; //packet_buffer[16]; // high order bits
  LBT = cmd.g7byte5; //packet_buffer[21]; // block number high
  LBL = cmd.g7byte4; //packet_buffer[20]; // block number middle
  LBN = cmd.g7byte3; //  packet_buffer[19]; // block number low
  block_num = (LBN & 0x7f) | (((unsigned short)LBH << 3) & 0x80);
  // block num second byte
  // print_packet ((unsigned char*) packet_buffer,get_packet_length());
  // Added (unsigned short) cast to ensure calculated block is not underflowing.
  block_num = block_num + (((LBL & 0x7f) | (((unsigned short)LBH << 4) & 0x80)) << 8);
  block_num = block_num + (((LBT & 0x7f) | (((unsigned short)LBH << 5) & 0x80)) << 16);
  Debug_printf(" Read block %04x", block_num);

  if (block_num != last_block_num + 1) // example optimization, only do seek if not reading next block -tschak
  {
    Debug_printf("\r\n");
    if (fseek(_disk->fileptr(), (block_num * 512), SEEK_SET))
    {
      Debug_printf("\r\nRead seek err! block #%02x", block_num);
      encode_error_reply_packet(SP_ERR_BADBLOCK);
      IWM.iwm_send_packet((unsigned char *)packet_buffer);
      return; // todo - send an error status packet?
    }
  }

  sdstato = fread((unsigned char *)packet_buffer, 1, 512, _disk->fileptr()); // Reading block from SD Card
  if (sdstato != 512)
  {
    Debug_printf("\r\nFile Read err: %d bytes", sdstato);
    encode_error_reply_packet(SP_ERR_IOERROR);
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
    return; // todo - true or false?
  }
  encode_data_packet();
  Debug_printf("\r\nsending block packet ...");
  if (!IWM.iwm_send_packet((unsigned char *)packet_buffer))
    last_block_num = block_num;
}

void iwmDisk::iwm_writeblock(cmdPacket_t cmd)
{
  uint8_t status = 0;
  uint8_t source = cmd.dest; // packet_buffer[6];
  // to do - actually we will already know that the cmd.dest == id(), so can just use id() here
  Debug_printf("\r\nDrive %02x ", source);
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  unsigned long int block_num = (cmd.g7byte3 & 0x7f) | (((unsigned short)cmd.grp7msb << 3) & 0x80);
  // block num second byte
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  block_num = block_num + (((cmd.g7byte4 & 0x7f) | (((unsigned short)cmd.grp7msb << 4) & 0x80)) * 256);
  Debug_printf("Write block %04x", block_num);
  //get write data packet, keep trying until no timeout
  // to do - this blows up - check handshaking
  if (IWM.iwm_read_packet_timeout(100, (unsigned char *)packet_buffer, BLOCK_PACKET_LEN))
  {
    Debug_printf("\r\nTIMEOUT in read packet!");
    return;
  }
  // partition number indicates which 32mb block we access
  if (decode_data_packet())
    iwm_return_ioerror(cmd);
  else
    { // ok
      //write block to CF card
      //Serial.print(F("\r\nWrite Bl. n.r: "));
      //Serial.print(block_num);
      if (block_num != last_block_num + 1) // example optimization, only do seek if not writing next block -tschak
      {
        Debug_printf("\r\n");
        if (fseek(_disk->fileptr(), (block_num * 512), SEEK_SET))
        {
          Debug_printf("\r\nRead seek err! block #%02x", block_num);
          encode_error_reply_packet(SP_ERR_BADBLOCK);
          IWM.iwm_send_packet((unsigned char *)packet_buffer);
          return; // todo - send an error status packet?
                  // to do - set a flag here to check for error status
        }
      }
      size_t sdstato = fwrite((unsigned char *)packet_buffer, 1, 512, _disk->fileptr());
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
      //Serial.print(F("\r\nSent status Packet Data\r\n") );
      //print_packet ((unsigned char*) sector_buffer,512);

      //print_packet ((unsigned char*) packet_buffer,get_packet_length());
      last_block_num = block_num;
    }
}



// void iwm_format();



// void derive_percom_block(uint16_t numSectors);
// void iwm_read_percom_block();
// void iwm_write_percom_block();
// void dump_percom_block();

void iwmDisk::shutdown()
{
}

iwmDisk::iwmDisk()
{
  Debug_printf("iwmDisk::iwmDisk()\n");
  // init();
}

mediatype_t iwmDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{

  mediatype_t mt = MEDIATYPE_UNKNOWN;

  Debug_printf("disk MOUNT %s\n", filename);

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    delete _disk;
    _disk = nullptr;
  }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_PO:
        Debug_printf("\r\nMedia Type PO");
        device_active = true;
        _disk = new MediaTypePO();
        mt = _disk->mount(f, disksize);
        //_disk->fileptr() = f;
        // mt = MEDIATYPE_PO;
        break;
    default:
        Debug_printf("\r\nMedia Type UNKNOWN - no mount");
        device_active = false;
        break;
    }

    return mt;

}

void iwmDisk::unmount()
  
{
  
}

bool iwmDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
  return false;
}

bool iwmDisk::write_blank(FILE *f, uint16_t numBlocks)
{
  return false;
}


/* void iwmDisk::startup_hack()
{
  // Debug_printf("\r\n Disk startup hack");
  // init();
}
 */
#endif /* BUILD_APPLE */