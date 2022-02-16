#ifdef BUILD_APPLE
#include "fuji.h"

#include "fnSystem.h"
#include "led.h"

iwmFuji theFuji; // Global fuji object.

iwmFuji::iwmFuji()
{
  Debug_printf("Announcing the iwmFuji::iwmFuji()!!!\n");
}

void iwmFuji::iwm_reset_fujinet()
{
    Debug_printf("\r\nFuji cmd: REBOOT");
    encode_status_reply_packet();
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
    fnSystem.reboot();
}

void iwmFuji::shutdown()
{
}

iwmDisk *iwmFuji::bootdisk()
{
    return nullptr;
}

void iwmFuji::insert_boot_device(uint8_t d)
{
}

void iwmFuji::setup(iwmBus *iwmbus)
{
}

void iwmFuji::image_rotate()
{
}
int iwmFuji::get_disk_id(int drive_slot)
{
    return -1;
}
std::string iwmFuji::get_host_prefix(int host_slot)
{
    return std::string();
}

void iwmFuji::_populate_slots_from_config()
{
}
void iwmFuji::_populate_config_from_slots()
{
}

void iwmFuji::sio_mount_all() // 0xD7 (yes, I know.)
{
}

void iwmFuji::process(cmdPacket_t cmd)
{
  fnLedManager.set(LED_BUS, true);
  switch (cmd.command)
  {
  case 0x80: // status
    Debug_printf("\r\nhandling status command");
    iwm_status(cmd);
    break;
  case 0x81: // read block
    iwm_return_badcmd(cmd);
    break;
  case 0x82: // write block
    iwm_return_badcmd(cmd);
    break;
  case 0x83: // format
    iwm_return_badcmd(cmd);
    break;
  case 0x84: // control
    Debug_printf("\r\nhandling control command");
    iwm_ctrl(cmd);
    break;
  case 0x86: // open
    Debug_printf("\r\nhandling open command");
    iwm_open(cmd);
    break;
  case 0x87: // close
    iwm_close(cmd);
    break;
  case 0x88: // read
    Debug_printf("\r\nhandling read command");
    iwm_read(cmd);
    break;
  case 0x89: // write
    iwm_return_badcmd(cmd);
    break;
  } // switch (cmd)
  fnLedManager.set(LED_BUS, false);
}

void iwmFuji::encode_status_reply_packet()
{

  uint8_t checksum = 0;
  uint8_t data[4];

  // Build the contents of the packet
  data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  data[1] = 0;         // block size 1
  data[2] = 0;  // block size 2
  data[3] = 0; // block size 3

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

void iwmFuji::encode_status_dib_reply_packet()
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
  data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  data[1] = 0;         // block size 1
  data[2] = 0;  // block size 2
  data[3] = 0; // block size 3
  data[4] = 0x08;                    // ID string length - 11 chars
  data[5] = 'T';
  data[6] = 'H';
  data[7] = 'E';
  data[8] = '_';
  data[9] = 'F';
  data[10] = 'U';
  data[11] = 'J';
  data[12] = 'I';
  data[13] = ' ';
  data[14] = ' ';
  data[15] = ' ';
  data[16] = ' ';
  data[17] = ' ';
  data[18] = ' ';
  data[19] = ' ';
  data[20] = ' ';  // ID string (16 chars total)
  data[21] = SP_TYPE_BYTE_FUJINET; // Device type    - 0x02  harddisk
  data[22] = SP_SUBTYPE_BYTE_FUJINET; // Device Subtype - 0x0a
  data[23] = 0x00; // Firmware version 2 bytes
  data[24] = 0x01; //

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

void iwmFuji::iwm_open(cmdPacket_t cmd)
{
  Debug_printf("\r\nOpen FujiNet Unit # %02x",cmd.g7byte1);
  encode_status_reply_packet();
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmFuji::iwm_close(cmdPacket_t cmd)
{
}

void iwmFuji::iwm_ctrl(cmdPacket_t cmd)
{
  uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];
  uint8_t control_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
  Debug_printf("\r\nDevice %02x Control Code %02x", source, control_code);
  Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
  IWM.iwm_read_packet_timeout(100, (uint8_t *)packet_buffer, BLOCK_PACKET_LEN);
  Debug_printf("\r\n There are %02x Odd Bytes and %02x 7-byte Groups",packet_buffer[11] & 0x7f, packet_buffer[12] & 0x7f);
  switch (control_code)
  {
  case 0x00:
  case 0xFF:
    iwm_reset_fujinet();
    break;
  default:
    break;
  }
  encode_status_reply_packet();
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmFuji::iwm_read(cmdPacket_t cmd)
{
  uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];

  uint16_t numbytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
  numbytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

  uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
  addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
  addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

  Debug_printf("\r\nDevice %02x Read %04x bytes from address %06x", source, numbytes, addy);


  // Debug_printf(" - ERROR - No image mounted");
  // encode_error_reply_packet(source, SP_ERR_OFFLINE);
  // IWM.iwm_send_packet((unsigned char *)packet_buffer);
  // return;

  memcpy(packet_buffer,"HELLO WORLD",11);
  encode_data_packet(source, 11);
  Debug_printf("\r\nsending data packet with %d elements ...", 11);
  //print_packet();
  IWM.iwm_send_packet((unsigned char *)packet_buffer);
}



#endif /* BUILD_APPLE */