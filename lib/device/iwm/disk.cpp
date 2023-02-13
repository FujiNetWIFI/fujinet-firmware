#ifdef BUILD_APPLE
#include "disk.h"

#include "fnSystem.h"
// #include "fnFsTNFS.h"
// #include "fnFsSD.h"
#include "led.h"
#include "fuji.h"

// #define LOCAL_TNFS

// FileSystemTNFS tserver;

iwmDisk::~iwmDisk()
{
}

uint8_t iwmDisk::smartport_device_type()
{
  if (_disk == nullptr)
    return SP_TYPE_BYTE_HARDDISK;

  if (_disk->num_blocks < 1601)
    return SP_TYPE_BYTE_35DISK; // Floppy disk
  else
    return SP_TYPE_BYTE_HARDDISK; // Hard disk
}

uint8_t iwmDisk::smartport_device_subtype()
{
  if (_disk == nullptr)
    return SP_SUBTYPE_BYTE_SWITCHED;
    
  if (_disk->num_blocks < 1601)
    return SP_SUBTYPE_BYTE_SWITCHED; // Floppy disk
  else
    return SP_SUBTYPE_BYTE_SWITCHED; // Hard Disk
}

//*****************************************************************************
// Function: send_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command packet. The reply
// includes following:
// data byte 1 is general info.
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Size determined from image file.
//*****************************************************************************
void iwmDisk::send_status_reply_packet()
{
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
  // Bit 0: Disk Switched 
  data[0] = 0b11101000;
  if((switched) && (!eject_latch)) {
    data[0] |= STATCODE_DISK_SWITCHED;
    switched = false;
    }
  if((!device_active) || (eject_latch)) {data[0] &= ~(STATCODE_DEVICE_ONLINE);}
  if(device_active) {data[0] |= STATCODE_DEVICE_ONLINE;}
  if(readonly) {data[0] |= STATCODE_WRITE_PROTECT;}
  if((_disk != nullptr) && (eject_latch)) {eject_latch = false;}
  Debug_printf("\r\nsend_status_reply_packet status = %02X\r\n",data[0]);
  data[1] = data[2] = data[3] = 0;
  if (_disk != nullptr)
  {
    data[0] |= (1 << 4);
    // Disk size
    data[1] = _disk->num_blocks & 0xff;
    data[2] = (_disk->num_blocks >> 8) & 0xff;
    data[3] = (_disk->num_blocks >> 16) & 0xff;
  }
  IWM.iwm_send_packet(id(),iwm_packet_type_t::status,SP_ERR_NOERROR, data, 4);
}

//*****************************************************************************
// Function: send_long_status_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the extended status command packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Size determined from image file.
//*****************************************************************************
void iwmDisk::send_extended_status_reply_packet() //XXX! Currently unused
{
  uint8_t data[5];
  data[0] = 0b11101000;
  if(!device_active) {data[0] &= ~(STATCODE_DEVICE_ONLINE);}
  if(device_active) {data[0] |= STATCODE_DEVICE_ONLINE;}
  if(switched) {
    data[0] |= STATCODE_DISK_SWITCHED;
    switched = false;
    }
  if(readonly) {data[0] |= STATCODE_WRITE_PROTECT;}
  Debug_printf("\r\nsend_extended_status_reply_packet status = %02X\r\n",data[0]);
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
  IWM.iwm_send_packet(id(), iwm_packet_type_t::ext_status, SP_ERR_NOERROR, data, 5);
}

//*****************************************************************************
// Function: send_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-4 number of blocks. 2 is the LSB and 4 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void iwmDisk::send_status_dib_reply_packet() // to do - abstract this out with passsed parameters
{
  uint8_t data[25];

  //* write data buffer first (25 bytes) 3 grp7 + 4 odds
  // General Status byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected (block devices only)
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Disk switched
  data[0] = 0b11101000;
  if((!device_active)|| (eject_latch)) {data[0] &= ~(STATCODE_DEVICE_ONLINE);}
  if(device_active) {data[0] |= STATCODE_DEVICE_ONLINE;}
  if(readonly) {data[0] |= STATCODE_WRITE_PROTECT;}
  Debug_printf("\r\nsend_status_dib_reply_packet status = %02X\r\n",data[0]);
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
  data[21] = smartport_device_type();
  data[22] = smartport_device_subtype();
  data[23] = 0x01; // Firmware version 2 bytes
  data[24] = 0x0f; //
  IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

//*****************************************************************************
// Function: send_long_status_dib_reply_packet
// Parameters: source
// Returns: none
//
// Description: this is the reply to the status command 03 packet. The reply
// includes following:
// data byte 1
// data byte 2-5 number of blocks. 2 is the LSB and 5 the MSB.
// Calculated from actual image file size.
//*****************************************************************************
void iwmDisk::send_extended_status_dib_reply_packet() //XXX! currently unused
{
  uint8_t data[25];

  //* write data buffer first (25 bytes) 3 grp7 + 4 odds
  // General Status byte
  // Bit 7: Block  device
  // Bit 6: Write allowed
  // Bit 5: Read allowed
  // Bit 4: Device online or disk in drive
  // Bit 3: Format allowed
  // Bit 2: Media write protected (block devices only)
  // Bit 1: Currently interrupting (//c only)
  // Bit 0: Disk Switched
  data[0] = 0b11101000;
  if(switched) {
    data[0] |= STATCODE_DISK_SWITCHED;
    switched = false;
    }
  if(!device_active) {data[0] &= ~(STATCODE_DEVICE_ONLINE);}
  if(device_active) {data[0] |= STATCODE_DEVICE_ONLINE;}
  if(readonly) {data[0] |= STATCODE_WRITE_PROTECT;}
  Debug_printf("\r\nsend extended DIB replay packet status = %02X\r\n",data[0]);
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
  IWM.iwm_send_packet(id(), iwm_packet_type_t::ext_status, SP_ERR_NOERROR, data, 25);
}
void iwmDisk::iwm_handle_eject(iwm_decoded_cmd_t cmd) {
  Debug_printf("Hanlding Eject command\r\n");
  unmount();
  //switched = false; //force switched = false when ejected from host. 
  iwm_return_noerror();
  return;
}
void iwmDisk::process(iwm_decoded_cmd_t cmd)
{
  uint8_t status_code;
  fnLedManager.set(LED_BUS, true);
  switch (cmd.command)
  {
  case 0x00: // status
    Debug_printf("\r\nhandling status command");
    status_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x00); // status codes 00-FF
    if (disk_num == '0' && status_code > 0x05) // max regular status code is 0x05 to UniDisk
      theFuji.FujiStatus(cmd);
    else  
      iwm_status(cmd);
    break;
  case 0x01: // read block
    Debug_printf("\r\nhandling read block command");
    iwm_readblock(cmd);
    break;
  case 0x02: // write block
    Debug_printf("\r\nhandling write block command");
    iwm_writeblock(cmd);
    break;
  case 0x03: // format
    iwm_return_noerror();
    break;
  case 0x04: // control
    status_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    if (disk_num == '0' && status_code > 0x0A) // max regular control code is 0x0A to 3.5" disk
      theFuji.FujiControl(cmd);
    else  
      iwm_handle_eject(cmd);
    break;
  case 0x06: // open
    iwm_return_badcmd(cmd);
    break;
  case 0x07: // close
    iwm_return_badcmd(cmd);
    break;
  case 0x08: // read
    iwm_return_badcmd(cmd);
    break;
  case 0x09: // write
    iwm_return_badcmd(cmd);
    break;
  default:
    iwm_return_badcmd(cmd);
  } // switch (cmd)
  fnLedManager.set(LED_BUS, false);
}

void iwmDisk::iwm_readblock(iwm_decoded_cmd_t cmd)
{
  // uint8_t LBH, LBL, LBN, LBT;
  uint32_t block_num;
  uint16_t sdstato;
  // uint8_t source;

  // source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];
  Debug_printf("\r\nDrive %02x ", id());
  if((switched) && (!eject_latch)){
    Debug_printf("iwm_readblock() returning disk switched error\r\n");
    send_reply_packet(SP_ERR_OFFLINE);
    switched = false;
    return;
  }
  if (!(_disk != nullptr))
  {
    Debug_printf(" - ERROR - No image mounted");
    send_reply_packet(SP_ERR_OFFLINE);
    return;
  }
  if((!device_active) || (eject_latch)) {
    Debug_printf("iwm_readblock while device offline!\r\n");
    send_reply_packet(SP_ERR_OFFLINE);
    if((_disk != nullptr) && (eject_latch))
      eject_latch = false;
    return;
  }

  
  // LBH = cmd.grp7msb; //packet_buffer[16]; // high order bits
  // LBT = cmd.g7byte5; //packet_buffer[21]; // block number high
  // LBL = cmd.g7byte4; //packet_buffer[20]; // block number middle
  // LBN = cmd.g7byte3; //  packet_buffer[19]; // block number low
  // block_num = (LBN & 0x7f) | (((unsigned short)LBH << 3) & 0x80);
  // // block num second byte
  // // print_packet ((unsigned char*) packet_buffer,get_packet_length());
  // // Added (unsigned short) cast to ensure calculated block is not underflowing.
  // block_num = block_num + (((LBL & 0x7f) | (((unsigned short)LBH << 4) & 0x80)) << 8);
  // block_num = block_num + (((LBT & 0x7f) | (((unsigned short)LBH << 5) & 0x80)) << 16);
  block_num = get_block_number(cmd);
  Debug_printf(" Read block %06x", block_num);

  sdstato = BLOCK_DATA_LEN;
  if (_disk->read(block_num, &sdstato, data_buffer))
  {
    Debug_printf("\r\nFile Seek or Read err: %d bytes", sdstato);
    send_reply_packet(SP_ERR_IOERROR);
    return; // todo - true or false?
  }
  
  // send_data_packet();
  Debug_printf("\r\nsending block packet ...");
  if (IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, BLOCK_DATA_LEN))
   ((MediaTypePO*)_disk)->reset_seek_opto();  // force seek next time if send error
}

void iwmDisk::iwm_writeblock(iwm_decoded_cmd_t cmd)
{
  uint8_t status = 0;
 
 
 //  uint8_t source = cmd.dest; // packet_buffer[6];
  // to do - actually we will already know that the cmd.dest == id(), so can just use id() here
  Debug_printf("\r\nDrive %02x ", id());
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  uint32_t block_num = get_block_number(cmd); // (cmd.g7byte3 & 0x7f) | (((unsigned short)cmd.grp7msb << 3) & 0x80);
  // block num second byte
  //Added (unsigned short) cast to ensure calculated block is not underflowing.
  // block_num = block_num + (((cmd.g7byte4 & 0x7f) | (((unsigned short)cmd.grp7msb << 4) & 0x80)) * 256);
  Debug_printf("Write block %06x", block_num);
  //get write data packet, keep trying until no timeout
  // to do - this blows up - check handshaking
  data_len = BLOCK_DATA_LEN;
  if (IWM.iwm_read_packet_timeout(100, (unsigned char *)data_buffer, data_len))
  {
    Debug_printf("\r\nTIMEOUT in read packet!");
    return;
  }
  // partition number indicates which 32mb block we access
  if (data_len == -1)
    iwm_return_ioerror();
  else
    { // We have to return the error after ingesting the block to write or ProDOS doesn't correctly see the status. 
      if((switched) && (!eject_latch)) {
        send_reply_packet(SP_ERR_OFFLINE);
        switched = false;
        return;
      }
      if((!device_active) || (eject_latch)) {
        Debug_printf("iwm_writeblock while device offline!\r\n");
        send_reply_packet(SP_ERR_OFFLINE);
        if((_disk != nullptr) && (eject_latch))
          eject_latch = false;
        return;
      } 
      if(readonly) {
        Debug_printf("\r\niwm_writeblock tried to write while readonly = true!");
        send_reply_packet(SP_ERR_NOWRITE);
        return;
      }

      uint16_t sdstato = BLOCK_DATA_LEN;
      _disk->write(block_num, &sdstato, data_buffer);
      
      if (sdstato != BLOCK_DATA_LEN)
      {
        Debug_printf("\r\nFile Write err: %d bytes", sdstato);
        if (sdstato == 0)
          status = 0x2B; // write protected todo: we should probably have a read-only flag that gets set and tested up top
        else
          status = 0x27; // 6;
        //return;
      }
      //now return status code to host
      send_reply_packet(status);
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
    /* We need  first eject the current disk image */
    unmount(); 
    eject_latch = true; //simulate the disk ejected for at least one status cycle.
                        //but only in the case we are mounting over an existing image.
  }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_PO:
        Debug_printf("\r\nMedia Type PO");
        _disk = new MediaTypePO();
        mt = _disk->mount(f, disksize);
        switched = true;
        device_active = true; //change status only after we are mounted
        //_disk->fileptr() = f;
        // mt = MEDIATYPE_PO;
        break;
      case MEDIATYPE_WOZ:
          theFuji._fnDisk2s[disk_num % 2].init();
          theFuji._fnDisk2s[disk_num % 2].mount(f); // modulo to ensure device 0 or 1
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
      if (_disk != nullptr)
    {
        _disk->unmount();
        delete _disk;
        _disk = nullptr;
        device_active = false;
        switched = true;
        readonly = true;
        Debug_printf("Disk UNMOUNTED!!!!\r\n");
    }
}

bool iwmDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
  
  return false;
}

/**
 * Used for writing ProDOS images which exist in multiples of 
 * 512 byte blocks.
 */
bool iwmDisk::write_blank(FILE *f, uint16_t numBlocks)
{
  unsigned char buf[512];

  memset(&buf,0,sizeof(buf));

  if (blank_header_type == 1) // 2MG
  {
    // Quickly construct and write 2MG header

    struct _2mg_header
    {
      unsigned char id[4] = {'2','I','M','G'};
      unsigned char creator[4] = {'F','U','J','I'};
      unsigned short header_size = 0x40; // 64 bytes
      unsigned short version = 0x01; 
      unsigned long format = 0x01; // Prodos order
      unsigned long flags = 0x00;
      unsigned long numBlocks = 0;
      unsigned long offset = 0x40UL; // Offset to disk data
      unsigned long len = (numBlocks * 512);
      unsigned long commentOffset = 0UL; // no comment.
      unsigned long commentLen = 0UL; // no comment.
      unsigned long creatorDataOffset = 0UL; // No creator data
      unsigned long creatorDataLen = 0UL; // No creator data
      unsigned char reserved[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    } header;

    Debug_printf("Writing 2MG Header\n");

    header.numBlocks = numBlocks;

    fwrite(&header,sizeof(header),1,f);
  }

  long offset = (numBlocks - 1) * 512;

  // Sparse Write
  fseek(f,offset,SEEK_SET);
  fwrite(&buf,sizeof(unsigned char),sizeof(buf),f);

  blank_header_type = 0; // Reset to unadorned.
  return false;
}


/* void iwmDisk::startup_hack()
{
  // Debug_printf("\r\n Disk startup hack");
  // init();
}
 */
#endif /* BUILD_APPLE */