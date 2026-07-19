#ifdef BUILD_APPLE
#include <array>
#include <cstdint>
#include <vector>

#include "disk.h"

#include "fnSystem.h"
// #include "fnFsTNFS.h"
// #include "fnFsSD.h"
#include "fsFlash.h"
#include "iwm/iwmFuji.h"

iwmDisk::~iwmDisk()
{
}

// Status Info byte
// Bit 7: Block  device
// Bit 6: Write allowed
// Bit 5: Read allowed
// Bit 4: Device online or disk in drive
// Bit 3: Format allowed
// Bit 2: Media write protected
// Bit 1: Currently interrupting (//c only)
// Bit 0: Disk Switched
uint8_t iwmDisk::create_status()
{
  uint8_t status = 0b11101000;
  status = (device_active) ? (status |   STATCODE_DEVICE_ONLINE) :
                             (status & ~(STATCODE_DEVICE_ONLINE));
  if (readonly)               status |=  STATCODE_WRITE_PROTECT;
  if (_disk != nullptr)       status |=  (1 << 4);

  return status;
}

std::vector<uint8_t> iwmDisk::create_blocksize(bool is_32_bits)
{
  std::vector<uint8_t> block_size;
  block_size.reserve(is_32_bits ? 4 : 3); // Reserve space for either 3 or 4 bytes

  if (_disk != nullptr)
  {
    block_size.push_back(static_cast<uint8_t>(_disk->num_blocks & 0xff));
    block_size.push_back(static_cast<uint8_t>((_disk->num_blocks >> 8) & 0xff));
    block_size.push_back(static_cast<uint8_t>((_disk->num_blocks >> 16) & 0xff));

    if (is_32_bits)
    {
        block_size.push_back(static_cast<uint8_t>((_disk->num_blocks >> 24) & 0xff));
    }

    Debug_printf("\r\nDIB number of blocks %lu\r\n", _disk->num_blocks);
  }
  else
  {
    // set vector all zeros of correct size
    block_size.resize(is_32_bits ? 4 : 3, 0);
  }

  return block_size;
}

uint8_t iwmDisk::smartport_device_type()
{
  if (_disk == nullptr)
    return prevtype; //report the last assigned type when offline.

  if (_disk->num_blocks < 1601) {
    prevtype = SP_TYPE_BYTE_35DISK;
    return SP_TYPE_BYTE_35DISK; // Floppy disk
  }
  else {
    prevtype = SP_TYPE_BYTE_HARDDISK;
    return SP_TYPE_BYTE_HARDDISK; // Hard disk
  }
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

iwm_device_status_block_t iwmDisk::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = create_status();
  if (_disk != nullptr)
    status.block_size = _disk->num_blocks;
  return status;
}

iwm_device_info_block_t iwmDisk::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  std::string name = "FUJINET_DISK_" + std::to_string(disk_num);
  dib.name_len = std::min(name.size(), sizeof(dib.name));
  std::memcpy(dib.name, name.data(), dib.name_len);

  dib.type = smartport_device_type();
  dib.subtype = smartport_device_subtype();
  dib.version = 0x0f01;

  return dib;
}

void iwmDisk::iwm_ctrl(const iwm_decoded_cmd_t &cmd)
{
  Debug_printf("\nDisk Device %02x Control Code %02x", id(), cmd.command());
  Debug_printf("\nDecoding Control Data Packet:");

  switch (cmd.frame.control_status.code)
  {
  case SP_CTRL_EJECT:
    Debug_printf("Handling Eject command\r\n");
    unmount();
    switched = false; //force switched = false when ejected from host.
    platformFuji.handle_ctl_eject(_devnum);
    break;
  default:
    SYSTEM_BUS.transaction_error(SP_ERR::BADCTL);
    break;
  }
}

void iwmDisk::iwm_readblock(const iwm_decoded_cmd_t &cmd)
{
  uint16_t sdstato;

  Debug_printf("\r\nDrive %02x ", id());

  Debug_printf(" Read block %06lx\r\n", cmd.frame.block_rw.num);
  if (!(_disk != nullptr))
  {
    Debug_printf(" - ERROR - No image mounted");
    SYSTEM_BUS.transaction_error(SP_ERR::OFFLINE);
    return;
  }
  if((!device_active)) {
    Debug_printf("iwm_readblock while device offline!\r\n");
    SYSTEM_BUS.transaction_error(SP_ERR::OFFLINE);
    return;
  }
  if((switched) && (cmd.frame.block_rw.num > 2)){
    Debug_printf("iwm_readblock() returning disk switched error\r\n");
    SYSTEM_BUS.transaction_error(SP_ERR::OFFLINE);
    switched = false;
    return;
  }
  Debug_printf("iwm_readblock NORMAL READ\r\n");
  switched = false; //if we made it here it's ok to reset switched

  sdstato = BLOCK_DATA_LEN;
  ByteBuffer buffer(sdstato);
  if (_disk->read(cmd.frame.block_rw.num, &sdstato, buffer.data()))
  {
    Debug_printf("\r\nFile Seek or Read err: %d bytes", sdstato);
    SYSTEM_BUS.transaction_error(SP_ERR::IOERROR);
    return; // todo - true or false?
  }

  // send_data_packet();
  Debug_printf("\r\nsending block packet ...");
  SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
  SYSTEM_BUS.transaction_send(buffer);
}

void iwmDisk::iwm_writeblock(const iwm_decoded_cmd_t &cmd)
{
  Debug_printf("\r\nDrive %02x ", id());
  Debug_printf("Write block %06lx", cmd.frame.block_rw.num);
  // partition number indicates which 32mb block we access
  // We have to return the error after ingesting the block to write or ProDOS doesn't correctly see the status.

  if((!device_active)) {
    Debug_printf("iwm_writeblock while device offline!\r\n");
    SYSTEM_BUS.transaction_error(SP_ERR::OFFLINE);
    return;
  }
  if(switched && readonly) {
    Debug_printf("iwm_writeblock while readonly and disk switched\r\n");
    SYSTEM_BUS.transaction_error(SP_ERR::NOWRITE);
    switched = false;
    return;
  }
  if(switched) {
    Debug_printf("iwm_writeblock while disk switched = true\r\nn");
    SYSTEM_BUS.transaction_error(SP_ERR::OFFLINE);
    switched = false;
    return;
  }
  if(readonly) {
    Debug_printf("\r\niwm_writeblock tried to write while readonly = true!");
    SYSTEM_BUS.transaction_error(SP_ERR::NOWRITE);
    return;
  }

  uint16_t sdstato = BLOCK_DATA_LEN;
  ByteBuffer buffer(sdstato, 0);
  SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
  SYSTEM_BUS.transaction_get(buffer.data(), buffer.size());
  _disk->write(cmd.frame.block_rw.num, &sdstato, buffer.data());

  if (sdstato != BLOCK_DATA_LEN)
  {
    Debug_printf("\r\nFile Write err: %d bytes", sdstato);
    if (sdstato == 0)
      SYSTEM_BUS.transaction_error(SP_ERR::NOWRITE); // write protected todo: we should probably have a read-only flag that gets set and tested up top
    else
      SYSTEM_BUS.transaction_error(SP_ERR::IOERROR);
    return;
  }

  SYSTEM_BUS.transaction_success();
}

void iwmDisk::iwm_format(const iwm_decoded_cmd_t &cmd)
{
  SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
  SYSTEM_BUS.transaction_success();
}

void iwmDisk::shutdown()
{
}

iwmDisk::iwmDisk()
{
  Debug_printf("iwmDisk::iwmDisk()\n");
  // init();
}

mediatype_t iwmDisk::mount(fnFile *f, const char *filename, uint32_t disksize,
                           disk_access_flags_t access_mode, mediatype_t disk_type)
{
  Debug_printf("disk MOUNT %s\n", filename);

  // Destroy any existing MediaType
  if (_disk != nullptr)
  {
    /* We need  first eject the current disk image */
    unmount();
    switched = true; //set disk switched only if we are mounting over an existing image.
  }

  // Determine MediaType based on filename extension
  if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr) {
      disk_type = MediaType::discover_mediatype(filename);
  }

  if (disk_type == MEDIATYPE_DSK) {
      // determine DO or PO based on file contents
      disk_type = MediaType::discover_dsk_mediatype(f, disksize);
  }

  mount_file(f, disksize, disk_type);

  if (disk_type == MEDIATYPE_UNKNOWN) {
      Debug_printf("\r\nMedia Type UNKNOWN - no mount in disk.cpp");
      device_active = false;
      is_config_device = false;
  }
  else if (_disk && strlen(_disk->_disk_filename))
      strcpy(_disk->_disk_filename, filename);

    if (access_mode & DISK_ACCESS_MODE_WRITE)
    {
        Debug_printv("Setting disk to read/write");
        readonly = false;
    }

  return disk_type;
}

mediatype_t iwmDisk::mount_file(fnFile *f, uint32_t disksize, mediatype_t disk_type)
{
  switch (disk_type)
    {
    case MEDIATYPE_DO:
      Debug_printf("\r\nMedia Type DO");
      _disk = new MediaTypeDO();
      break;
    case MEDIATYPE_PO:
      Debug_printf("\r\nMedia Type PO");
      _disk = new MediaTypePO();
      break;
    default:
      Debug_printf("\r\nUnsupported Media Type for SmartPort");
      disk_type = MEDIATYPE_UNKNOWN;
      break;
    }

  if (disk_type != MEDIATYPE_UNKNOWN) {
    _disk->_mediatype = disk_type;
    disk_type = _disk->mount(f, disksize);
  }

  if (disk_type != MEDIATYPE_UNKNOWN) {
    // firmware needs to believe a high score enabled disk is
    // not write-protected. Otherwise it will skip write process
    if (_disk->high_score_enabled)
      readonly = false;

    device_active = true; //change status only after we are mounted
    is_config_device = false;
  }

  return disk_type;
}

void iwmDisk::unmount()
{
    if (_disk != nullptr)
    {
        _disk->unmount();
        delete _disk;
        _disk = nullptr;
        if (device_active)
            switched = true;
        device_active = false;
        is_config_device = false;
        readonly = true;
        Debug_printf("Disk UNMOUNTED!!!!\r\n");
    }
}

// ProDOS Filesystem Creation Functions and Constants

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define PRODOS_BLOCK_SIZE       512u    /* bytes per block                   */

#define VOL_DIR_KEY_BLOCK       2u      /* first block of volume directory   */
#define VOL_DIR_NUM_BLOCKS      4u      /* volume directory spans 4 blocks   */
#define BITMAP_START_BLOCK      6u      /* volume bitmap begins here         */

#define ENTRY_LENGTH            0x27u   /* 39 bytes per directory entry      */
#define ENTRIES_PER_BLOCK       0x0Du   /* 13 directory entries per block    */

/*
 * Access byte for a freshly formatted volume:
 *   Bit 7 (0x80) – D: destroy enable
 *   Bit 6 (0x40) – N: rename enable
 *   Bit 2 (0x02) – W: write enable
 *   Bit 0 (0x01) – R: read enable
 */
#define DEFAULT_ACCESS          0xC3u

/* High nibble of the first byte of the volume directory header entry */
#define VOL_HDR_STORAGE_TYPE    0xF0u

/*
 * prodos_encode_datetime – encode the current local wall-clock time into
 * the two 16-bit ProDOS date/time words.
 *
 * Date word layout (little-endian in the directory):
 *   Bits 15-9  : year  (tm_year value, i.e. years since 1900, 7-bit field)
 *   Bits  8-5  : month (1-12)
 *   Bits  4-0  : day   (1-31)
 *
 * Time word layout:
 *   Bits 12-8  : hour   (0-23)
 *   Bits  5-0  : minute (0-59)
 */
void iwmDisk::prodos_encode_datetime(unsigned short *date_out, unsigned short *time_out)
{
    time_t      now = time(NULL);
    struct tm  *t   = localtime(&now);

    *date_out = (unsigned short)(
          (((unsigned)t->tm_year  & 0x7Fu) << 9)
        | (((unsigned)(t->tm_mon + 1) & 0x0Fu) << 5)
        |  ((unsigned)t->tm_mday  & 0x1Fu));

    *time_out = (unsigned short)(
          (((unsigned)t->tm_hour & 0x1Fu) << 8)
        |  ((unsigned)t->tm_min  & 0x3Fu));
}

/*
 * write_block – write one 512-byte block to *fp.
 * Returns 0 on success, -1 on I/O error.
 */
int iwmDisk::prodos_write_block(fnFile *f, const unsigned char *buf)
{
    return (fnio::fwrite(buf, sizeof(unsigned char), PRODOS_BLOCK_SIZE, f) == PRODOS_BLOCK_SIZE) ? 0 : -1;
}

/**
 * @brief write prodos boot sector
 * @param f file to write to
 * @return true if error, false if success
 */
error_is_true iwmDisk::prodos_write_boot_block(fnFile *f)
{
  unsigned char buf[PRODOS_BLOCK_SIZE];
  memset(&buf,0,sizeof(buf));

  FILE *sf = fsFlash.file_open("/prodos_boot_block.bin","rb");
  if (!sf)
  {
    Debug_printf("Could not open /prodos_boot_block.bin. Aborting.\n");
    fclose(sf);
    RETURN_ERROR_AS_TRUE();
  }

  if (fread(buf,sizeof(unsigned char),sizeof(buf),sf) != sizeof(buf))
  {
    Debug_printf("Short read of prodos_boot_block.bin, aborting.\n");
    fclose(sf);
    RETURN_ERROR_AS_TRUE();
  }
  if (fnio::fwrite(buf,sizeof(unsigned char),sizeof(buf),f) != sizeof(buf))
  {
    Debug_printf("Short write to destination image. Aborting.\n");
    fclose(sf);
    RETURN_ERROR_AS_TRUE();
  }

  fclose(sf);
  Debug_printf("ProDOS boot block written successfully.\n");
  RETURN_SUCCESS_AS_FALSE();
}

/**
 * @brief write prodos SOS sector
 * @param f file to write to
 * @return true if error, false if success
 */
error_is_true iwmDisk::prodos_write_sos_block(fnFile *f)
{
  unsigned char buf[PRODOS_BLOCK_SIZE];
  memset(&buf,0,sizeof(buf));

  if (fnio::fwrite(buf,sizeof(unsigned char),sizeof(buf),f) != sizeof(buf))
  {
    Debug_printf("Short write to destination image. Aborting.\n");
    RETURN_ERROR_AS_TRUE();
  }

  Debug_printf("ProDOS SOS block written successfully.\n");
  RETURN_SUCCESS_AS_FALSE();
}

error_is_true iwmDisk::prodos_write_directory_sectors(fnFile *f, uint16_t numBlocks, const char *label)
{
  unsigned char block[PRODOS_BLOCK_SIZE];
  unsigned short cr_date, cr_time;
  size_t nameLen = (label) ? strnlen(label,15) : 0;

  prodos_encode_datetime(&cr_date, &cr_time);

  for (unsigned int b = VOL_DIR_KEY_BLOCK; b < VOL_DIR_KEY_BLOCK + VOL_DIR_NUM_BLOCKS; b++)
  {
    memset(block,0,sizeof(block));

    /* block link pointers */
    if (b > VOL_DIR_KEY_BLOCK) 
    {
      block[0] = (unsigned char)((b - 1u) & 0xFFu);
      block[1] = (unsigned char)((b - 1u) >> 8);
    }
    
    if (b < VOL_DIR_KEY_BLOCK + VOL_DIR_NUM_BLOCKS - 1u) 
    {
      block[2] = (unsigned char)((b + 1u) & 0xFFu);
      block[3] = (unsigned char)((b + 1u) >> 8);
    }

    /* Volume Directory Header – key block only */
    if (b == VOL_DIR_KEY_BLOCK) 
    {
      unsigned char *h = &block[4];   /* h[N] = entry offset +N */

      h[0]  = (unsigned char)(VOL_HDR_STORAGE_TYPE | (nameLen & 0x0Fu));
      memcpy(&h[1], label, nameLen);   /* +1..+15: name, zero-padded */
      /* +16..+17: RESERVED ($0000 – already zero from memset) */

      h[18] = (unsigned char)(cr_date & 0xFFu); /* +18..+19: CREAT_DATE */
      h[19] = (unsigned char)(cr_date >> 8);
      h[20] = (unsigned char)(cr_time & 0xFFu); /* +20..+21: CREAT_TIME */
      h[21] = (unsigned char)(cr_time >> 8);

      h[22] = 0x00;   /* +22: VERSION     */
      h[23] = 0x00;   /* +23: MIN_VERSION */

      h[24] = (unsigned char)(cr_date & 0xFFu); /* +24..+25: MOD_DATE   */
      h[25] = (unsigned char)(cr_date >> 8);
      h[26] = (unsigned char)(cr_time & 0xFFu); /* +26..+27: MOD_TIME   */
      h[27] = (unsigned char)(cr_time >> 8);

      /* +28: last directory block number (blocks 2..5, so last = 5) */
      h[28] = (unsigned char)(VOL_DIR_KEY_BLOCK + VOL_DIR_NUM_BLOCKS - 1u);
      /* +29: reserved/padding ($00 – already zero) */

      h[30] = DEFAULT_ACCESS;     /* +30: ACCESS           */
      h[31] = ENTRY_LENGTH;       /* +31: ENTRY_LENGTH     */
      h[32] = ENTRIES_PER_BLOCK;  /* +32: ENTRIES_PER_BLOCK*/
      /* +33..+34: FILE_COUNT ($0000 – already zero) */

      h[35] = (unsigned char)(BITMAP_START_BLOCK & 0xFFu); /* +35..+36 */
      h[36] = (unsigned char)(BITMAP_START_BLOCK >> 8);

      h[37] = (unsigned char)((unsigned)numBlocks & 0xFFu); /* +37..+38 */
      h[38] = (unsigned char)((unsigned)numBlocks >> 8);
    }

    if (prodos_write_block(f, block)<0)
    {
      Debug_printf("Short write to destination image. Aborting.\n");
      RETURN_ERROR_AS_TRUE();
    }
  }

  RETURN_SUCCESS_AS_FALSE();
}

/**
 * @brief write bitmap blocks for a ProDOS volume
 * @param f file to write to
 * @param numBlocks total number of blocks on the volume
 * @return true if error, false if success
 */
error_is_true iwmDisk::prodos_write_bitmap(fnFile *f, uint16_t numBlocks)
{
    unsigned int bitmapBytes       = ((unsigned int)numBlocks + 7u) / 8u;
    unsigned int bitmapBlocks      = (bitmapBytes + PRODOS_BLOCK_SIZE - 1u) / PRODOS_BLOCK_SIZE;
    unsigned int totalSystemBlocks = 2u + VOL_DIR_NUM_BLOCKS + bitmapBlocks;
    unsigned char *bitmap          = (unsigned char *)malloc(bitmapBlocks * PRODOS_BLOCK_SIZE);
    unsigned int b=0, i=0;

    if (!bitmap)
    {
        Debug_printf("Failed to allocate memory for ProDOS bitmap.\n");
        RETURN_ERROR_AS_TRUE();
    }
    else
        Debug_printf("ProDOS bitmap: numBlocks=%u, bitmapBytes=%u, bitmapBlocks=%u, totalSystemBlocks=%u\n",
                     (unsigned)numBlocks, bitmapBytes, bitmapBlocks, totalSystemBlocks);

    memset(bitmap, 0xFFu, bitmapBlocks * PRODOS_BLOCK_SIZE);

    /* Mark system blocks as used */
    for (i = 0; i < totalSystemBlocks; i++) {
        bitmap[i / 8u] &= (unsigned char)~(1u << (7u - (i % 8u)));
    }
    /* Clear phantom bits beyond numBlocks */
    for (i = (unsigned int)numBlocks; i < bitmapBlocks * PRODOS_BLOCK_SIZE * 8u; i++) {
        bitmap[i / 8u] &= (unsigned char)~(1u << (7u - (i % 8u)));
    }

    for (b = 0; b < bitmapBlocks; b++) {
      if (prodos_write_block(f, bitmap + b * PRODOS_BLOCK_SIZE) < 0)
      {
        Debug_printf("Short write to destination image. Aborting.\n");
        free(bitmap);
        RETURN_ERROR_AS_TRUE();
      }
    }

    free(bitmap);
    Debug_printf("ProDOS bitmap blocks written successfully.\n");
    RETURN_SUCCESS_AS_FALSE();
}

/**
 * @brief write data blocks for a ProDOS volume
 * @param f file to write to
 * @param numBlocks total number of blocks on the volume
 * @verbose Uses a sparse write.
 * @return true if error, false if success
 */
error_is_true iwmDisk::prodos_write_data_blocks(fnFile *f, uint16_t numBlocks)
{
  unsigned char buf[PRODOS_BLOCK_SIZE];
  unsigned long offset = (numBlocks - 1) * PRODOS_BLOCK_SIZE;
  
  memset(&buf,0,sizeof(buf));

  fnio::fseek(f,offset,SEEK_SET);
  if (fnio::fwrite(buf,sizeof(unsigned char),sizeof(buf),f) != sizeof(buf))
  {
    Debug_printf("Short write to destination image. Aborting.\n");
    RETURN_ERROR_AS_TRUE();
  }

  Debug_printf("ProDOS data blocks written successfully.\n");

  RETURN_SUCCESS_AS_FALSE();
}

static unsigned char random_hex_digit()
{
  const char hex_digits[] = "0123456789ABCDEF";
  return hex_digits[rand() % 16];
}

static const char *prodos_generate_temp_name()
{
  static char temp_name[16];
  snprintf(temp_name, sizeof(temp_name), "NEWDISK.%c%c%c%c", random_hex_digit(), random_hex_digit(), random_hex_digit(), random_hex_digit());
  return temp_name;
}

/**
 * Used for writing ProDOS images which exist in multiples of
 * 512 byte blocks.
 */
error_is_true iwmDisk::write_blank(fnFile *f, uint16_t numBlocks, uint8_t blank_header_type)
{
  unsigned char buf[512];

  memset(&buf,0,sizeof(buf));

  if (blank_header_type == 2) // DO
  {
    FILE *sf = fsFlash.file_open("/blank.do","rb");
    if (!sf)
    {
      Debug_printf("Could not open /blank.do. Aborting.\n");
      fclose(sf);
      RETURN_ERROR_AS_TRUE();
    }

    while (!feof(sf))
    {
      if (fread(buf,sizeof(unsigned char),sizeof(buf),sf) != sizeof(buf))
      {
        Debug_printf("Short read of blank.do, aborting.\n");
        fclose(sf);
        RETURN_ERROR_AS_TRUE();
      }
      if (fnio::fwrite(buf,sizeof(unsigned char),sizeof(buf),f) != sizeof(buf))
      {
        Debug_printf("Short write to destination image. Aborting.\n");
        fclose(sf);
        RETURN_ERROR_AS_TRUE();
      }
    }

    fclose(sf);
    Debug_printf("Creation of new DOS 3.3 disk successful.\n");
    RETURN_SUCCESS_AS_FALSE();
  }

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
    
    fnio::fwrite(&header,sizeof(header),1,f);
  }

  if (prodos_write_boot_block(f))
    RETURN_ERROR_AS_TRUE();
  if (prodos_write_sos_block(f))
    RETURN_ERROR_AS_TRUE();
  if (prodos_write_directory_sectors(f, numBlocks, prodos_generate_temp_name()))
    RETURN_ERROR_AS_TRUE();
  if (prodos_write_bitmap(f, numBlocks))
    RETURN_ERROR_AS_TRUE();
  if (prodos_write_data_blocks(f, numBlocks))
    RETURN_ERROR_AS_TRUE();

  Debug_printf("Creation of new ProDOS disk successful.\n");

  RETURN_SUCCESS_AS_FALSE();
}

// End of ProDOS image creation functions.

/* void iwmDisk::startup_hack()
{
  // Debug_printf("\r\n Disk startup hack");
  // init();
}
 */
#endif /* BUILD_APPLE */

/*
  Local Variables:
  mode: c++
  indent-tabs-mode: nil
  c-basic-offset: 2
  c-file-offsets: ((substatement-open . 0))
  End:
*/
