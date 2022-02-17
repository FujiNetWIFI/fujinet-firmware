#ifdef BUILD_APPLE
#include "fuji.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "led.h"
#include "fnWiFi.h"

#define IWM_CTRL_RESET 0x00


#define IWM_FUJICMD_RESET 0xFF
#define IWM_FUJICMD_GET_SSID 0xFE
#define IWM_FUJICMD_SCAN_NETWORKS 0xFD
#define IWM_FUJICMD_GET_SCAN_RESULT 0xFC
#define IWM_FUJICMD_SET_SSID 0xFB
#define SIO_FUJICMD_GET_WIFISTATUS 0xFA
#define SIO_FUJICMD_MOUNT_HOST 0xF9
#define SIO_FUJICMD_MOUNT_IMAGE 0xF8
#define SIO_FUJICMD_OPEN_DIRECTORY 0xF7
#define SIO_FUJICMD_READ_DIR_ENTRY 0xF6
#define SIO_FUJICMD_CLOSE_DIRECTORY 0xF5
#define SIO_FUJICMD_READ_HOST_SLOTS 0xF4
#define SIO_FUJICMD_WRITE_HOST_SLOTS 0xF3
#define SIO_FUJICMD_READ_DEVICE_SLOTS 0xF2
#define SIO_FUJICMD_WRITE_DEVICE_SLOTS 0xF1
#define SIO_FUJICMD_UNMOUNT_IMAGE 0xE9
#define SIO_FUJICMD_GET_ADAPTERCONFIG 0xE8
#define SIO_FUJICMD_NEW_DISK 0xE7
#define SIO_FUJICMD_UNMOUNT_HOST 0xE6
#define SIO_FUJICMD_GET_DIRECTORY_POSITION 0xE5
#define SIO_FUJICMD_SET_DIRECTORY_POSITION 0xE4
#define SIO_FUJICMD_SET_HSIO_INDEX 0xE3
#define SIO_FUJICMD_SET_DEVICE_FULLPATH 0xE2
#define SIO_FUJICMD_SET_HOST_PREFIX 0xE1
#define SIO_FUJICMD_GET_HOST_PREFIX 0xE0
#define SIO_FUJICMD_SET_SIO_EXTERNAL_CLOCK 0xDF
#define SIO_FUJICMD_WRITE_APPKEY 0xDE
#define SIO_FUJICMD_READ_APPKEY 0xDD
#define SIO_FUJICMD_OPEN_APPKEY 0xDC
#define SIO_FUJICMD_CLOSE_APPKEY 0xDB
#define SIO_FUJICMD_GET_DEVICE_FULLPATH 0xDA
#define SIO_FUJICMD_CONFIG_BOOT 0xD9
#define SIO_FUJICMD_COPY_FILE 0xD8
#define SIO_FUJICMD_MOUNT_ALL 0xD7
#define SIO_FUJICMD_SET_BOOT_MODE 0xD6
#define SIO_FUJICMD_ENABLE_DEVICE 0xD5
#define SIO_FUJICMD_DISABLE_DEVICE 0xD4
#define SIO_FUJICMD_STATUS 0x53
#define SIO_FUJICMD_HSIO_INDEX 0x3F

iwmFuji theFuji; // Global fuji object.

iwmFuji::iwmFuji()
{
  Debug_printf("Announcing the iwmFuji::iwmFuji()!!!\n");
}

void iwmFuji::iwm_dummy_command()
{
  for (int i=0; i<num_decoded; i++)
    Debug_printf(" %02x", packet_buffer[i]);
}

void iwmFuji::iwm_reset_fujinet()
{
    Debug_printf("\r\nFuji cmd: REBOOT");
    encode_status_reply_packet();
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
    fnSystem.reboot();
}

void iwmFuji::iwm_net_get_ssid()
{
   Debug_println("Fuji cmd: GET SSID");

    // Response to SIO_FUJICMD_GET_SSID
    struct
    {
        char ssid[MAX_SSID_LEN];
        char password[MAX_WIFI_PASS_LEN];
    } cfg;

    memset(&cfg, 0, sizeof(cfg));

    /*
     We memcpy instead of strcpy because technically the SSID and phasephras aren't strings and aren't null terminated,
     they're arrays of bytes officially and can contain any byte value - including a zero - at any point in the array.
     However, we're not consistent about how we treat this in the different parts of the code.
    */
    std::string s = Config.get_wifi_ssid();
    memcpy(cfg.ssid, s.c_str(),
           s.length() > sizeof(cfg.ssid) ? sizeof(cfg.ssid) : s.length());

    s = Config.get_wifi_passphrase();
    memcpy(cfg.password, s.c_str(),
           s.length() > sizeof(cfg.password) ? sizeof(cfg.password) : s.length());

    // Move into response.
    memcpy(response, &cfg, sizeof(cfg));
    response_len = sizeof(cfg);
} // 0xFE

void iwmFuji::iwm_net_scan_networks()
{
    Debug_println("Fuji cmd: SCAN NETWORKS");

    isReady = false;

    if (scanStarted == false)
    {
        _countScannedSSIDs = fnWiFi.scan_networks();
        scanStarted = true;
        setSSIDStarted = false;
    }

    isReady = true;

    response[0] = _countScannedSSIDs;
    response_len = 1;
} // 0xFD

void iwmFuji::iwm_net_scan_result()
{
    Debug_println("Fuji cmd: GET SCAN RESULT");
    scanStarted = false;

    uint8_t n = packet_buffer[0]; // adamnet_recv();
    // to do - this must come from the host in the control command?
    // adamnet_recv(); // get CK

    // Response to SIO_FUJICMD_GET_SCAN_RESULT
    struct
    {
        char ssid[MAX_SSID_LEN];
        uint8_t rssi;
    } detail;

    memset(&detail, 0, sizeof(detail));

    if (n < _countScannedSSIDs)
        fnWiFi.get_scan_result(n, detail.ssid, &detail.rssi);

    Debug_printf("SSID: %s - RSSI: %u\n", detail.ssid, detail.rssi);

    memset(response, 0, sizeof(response));
    memcpy(response, &detail, sizeof(detail));
    response_len = 33;
} // 0xFC

void iwmFuji::iwm_net_set_ssid()
{
 if (!fnWiFi.connected() && setSSIDStarted == false)
    {
        Debug_println("Fuji cmd: SET SSID");

        uint16_t s = 10; // to do - set up "s"
        s--;

        // Data for SIO_FUJICMD_SET_SSID
        struct
        {
            char ssid[MAX_SSID_LEN];
            char password[MAX_WIFI_PASS_LEN];
        } cfg;

        // to do - copy data over to cfg
        memcpy((uint8_t *)&cfg, (uint8_t *)packet_buffer, s);
        // adamnet_recv_buffer((uint8_t *)&cfg, s);

            bool save = true;

        Debug_printf("Connecting to net: %s password: %s\n", cfg.ssid, cfg.password);

        fnWiFi.connect(cfg.ssid, cfg.password);
        setSSIDStarted = true;
        // Only save these if we're asked to, otherwise assume it was a test for connectivity
        if (save)
        {
            Config.store_wifi_ssid(cfg.ssid, sizeof(cfg.ssid));
            Config.store_wifi_passphrase(cfg.password, sizeof(cfg.password));
            Config.save();
        }
    }
} // 0xFB

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
    Debug_printf("\r\nhandling close command");
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
  Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, packet_buffer[12] & 0x7f);
  decode_data_packet();
  print_packet((uint8_t *)packet_buffer);

  switch (control_code)
  {
  case 0xAA:
    iwm_dummy_command();
    break;
  case IWM_CTRL_RESET:
  case IWM_FUJICMD_RESET:
    iwm_reset_fujinet();
    break;
  case IWM_FUJICMD_GET_SSID:
    iwm_net_get_ssid(); // 0xFE
    break;
  case IWM_FUJICMD_SCAN_NETWORKS:
    iwm_net_scan_networks(); // 0xFD
    break;
  case IWM_FUJICMD_GET_SCAN_RESULT:
    iwm_net_scan_result(); // 0xFC
    break;
  case IWM_FUJICMD_SET_SSID:
    iwm_net_set_ssid(); // 0xFB
    break;
  default:
    // to do - send bad CTRL error
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