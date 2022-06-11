#ifdef BUILD_APPLE
#include "disk2.h"

#include "fnSystem.h"
#include "fnFsTNFS.h"
#include "fnFsSD.h"
#include "led.h"
#include "fuji.h"

#define LOCAL_TNFS

FileSystemTNFS tserver;

iwmDisk2::~iwmDisk2()
{
}

unsigned long int last_block_num=0xFFFFFFFF;
/* 
void iwmDisk2::iwm_readblock(cmdPacket_t cmd)
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
    IWM.SEND_PACKET((unsigned char *)packet_buffer);
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
      IWM.SEND_PACKET((unsigned char *)packet_buffer);
      return; // todo - send an error status packet?
    }
  }

  sdstato = fread((unsigned char *)packet_buffer, 1, 512, _disk->fileptr()); // Reading block from SD Card
  if (sdstato != 512)
  {
    Debug_printf("\r\nFile Read err: %d bytes", sdstato);
    encode_error_reply_packet(SP_ERR_IOERROR);
    IWM.SEND_PACKET((unsigned char *)packet_buffer);
    return; // todo - true or false?
  }
  encode_data_packet();
  Debug_printf("\r\nsending block packet ...");
  if (!IWM.SEND_PACKET((unsigned char *)packet_buffer))
    last_block_num = block_num;
}

void iwmDisk2::iwm_writeblock(cmdPacket_t cmd)
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
          IWM.SEND_PACKET((unsigned char *)packet_buffer);
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
      IWM.SEND_PACKET((unsigned char *)packet_buffer);
      //Serial.print(F("\r\nSent status Packet Data\r\n") );
      //print_packet ((unsigned char*) sector_buffer,512);

      //print_packet ((unsigned char*) packet_buffer,get_packet_length());
      last_block_num = block_num;
    }
}
 */
void iwmDisk2::shutdown()
{
}

iwmDisk2::iwmDisk2()
{
  Debug_printf("iwmDisk2::iwmDisk2()\n");
  // init();
}

mediatype_t iwmDisk2::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
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

void iwmDisk2::unmount()
{

}

bool iwmDisk2::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
  return false;
}

bool iwmDisk2::write_blank(FILE *f, uint16_t numBlocks)
{
  return false;
}

#endif /* BUILD_APPLE */