#include "disk.h"

int command_frame_counter = 0;

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

// Read
void sioDisk::sio_read()
{
  int ss;
  int sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  int cacheOffset = 0;
  int cacheSectorIndex;
  int cacheSetorIndexAdjust = 1;
  int offset;
  byte *s;
  byte *d;
  byte err = false;

  if (sectorNum <= UNCACHED_REGION)
  {
    ss = (sectorNum <= 3 ? 128 : sectorSize);
    offset = sectorNum;
    offset *= ss;
    offset -= ss;
    offset += 16;

    // Bias adjustment for 256 bytes
    if (ss == 256)
      offset -= 384;

    _file->seek(offset);     //tnfs_seek(deviceSlot, offset);
    _file->read(sector, ss); // tnfs_read(deviceSlot, 128);
    //d = &sector[0];
    //s = &tnfsPacket.data[3];
    //memcpy(d, s, ss);
  }
  else if (sectorNum >= 4)
  {
    max_cached_sectors = (sectorSize == 256 ? 9 : 19);
    if ((sectorNum > (firstCachedSector + max_cached_sectors)) || (sectorNum < firstCachedSector)) // cache miss
    {
      firstCachedSector = sectorNum;
      cacheOffset = 0;

      ss = sectorSize;

      offset = sectorNum;
      offset *= ss;
      offset -= ss;

      // Bias adjustment for 256 bytes
      if (ss == 256)
        offset -= 384;

      offset += 16;

#ifdef DEBUG
      Debug_printf("firstCachedSector: %d\n", firstCachedSector);
      Debug_printf("cacheOffset: %d\n", cacheOffset);
      Debug_printf("offset: %d\n", offset);
      Debug_printf("sectorSize: %d\n", sectorSize);
#endif

      _file->seek(offset); //tnfs_seek(deviceSlot, offset);

      for (unsigned char i = 0; i < 10; i++)
      {
        int l = _file->read(sector, 256);
        d = &sectorCache[cacheOffset];
        memcpy(d, sector, 256);

        if (l != -1)
        {
          cacheError[i] = false;
        }
        else
        {
          cacheError[i] = true;
          invalidate_cache();
          memset(&sectorCache, 0, sizeof(sectorCache));
          err = true;
        }
        cacheOffset += 256;
      }
      cacheOffset = 0;
    }
    else // cache hit, adjust offset
    {
      if (sectorNum < 4)
        ss = 128;
      else
        ss = sectorSize;

      cacheSectorIndex = (sectorNum - firstCachedSector);
      cacheOffset = (cacheSectorIndex * ss);

      if (ss == 128)
        cacheSetorIndexAdjust = 1;
      else
        cacheSetorIndexAdjust = 0;

      err = cacheError[cacheSectorIndex >> cacheSetorIndexAdjust];

#ifdef DEBUG
      Debug_printf("sectorIndex: %d\n", cacheSectorIndex);
      Debug_printf("cacheError: %d\n", cacheError[cacheSectorIndex]);
      Debug_printf("cacheOffset: %d\n", cacheOffset);
#endif
    }
    s = &sectorCache[cacheOffset];
    memcpy(sector, s, ss);
  }

  sio_to_computer((byte *)&sector, ss, err);
}

// write for W & P commands
void sioDisk::sio_write()
{

  byte ck;
  int ss; // sector size
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  int sectorNum = offset;
  // unsigned char deviceSlot = cmdFrame.devic - 0x31;

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
    offset *= sectorSize;
    offset -= sectorSize;
    ss = sectorSize;

    // Bias adjustment for 256 bytes
    if (ss == 256)
      offset -= 384;

    offset += 16; // skip 16 byte ATR Header
  }

  memset(sector, 0, 256); // clear buffer

  ck = sio_to_peripheral(sector, ss);

  if (ck == sio_checksum(sector, ss))
  {
    if (_file->seek(offset)) // tnfs_seek(deviceSlot, offset);
    {
      size_t sz = _file->write(sector, ss); // tnfs_write(deviceSlot, ss);
      if (ss == sz)
      {
        _file->flush();
        firstCachedSector = 65535; // invalidate cache

        sio_complete();
        return;
      }
    }
  }
  sio_error();
}

// Status
void sioDisk::sio_status()
{

  byte status[4] = {0x10, 0xDF, 0xFE, 0x00};
  //byte deviceSlot = cmdFrame.devic - 0x31;

  if (sectorSize == 256)
  {
    status[0] |= 0x20;
  }

  // todo:
  if (percomBlock.sectors_per_trackL == 26)
  {
    status[0] |= 0x80;
  }

  sio_to_computer(status, sizeof(status), false); // command always completes.
}

// fake disk format
void sioDisk::sio_format()
{
  // Populate bad sector map (no bad sectors)
  for (int i = 0; i < sectorSize; i++)
    sector[i] = 0;

  sector[0] = 0xFF; // no bad sectors.
  sector[1] = 0xFF;

  // Send to computer
  sio_to_computer((byte *)sector, sectorSize, false);

#ifdef DEBUG_S
  BUG_UART.printf("We faked a format.\n");
#endif
}

// ****************************************************************************************

/**
   Update PERCOM block from the total # of sectors.
*/
void sioDisk::derive_percom_block(unsigned short numSectors)
{
  // Start with 40T/1S 720 Sectors, sector size passed in
  percomBlock.num_tracks = 40;
  percomBlock.step_rate = 1;
  percomBlock.sectors_per_trackM = 0;
  percomBlock.sectors_per_trackL = 18;
  percomBlock.num_sides = 0;
  percomBlock.density = 0; // >128 bytes = MFM
  percomBlock.sector_sizeM = (sectorSize == 256 ? 0x01 : 0x00);
  percomBlock.sector_sizeL = (sectorSize == 256 ? 0x00 : 0x80);
  percomBlock.drive_present = 255;
  percomBlock.reserved1 = 0;
  percomBlock.reserved2 = 0;
  percomBlock.reserved3 = 0;

  if (numSectors == 1040) // 5/25" 1050 density
  {
    percomBlock.sectors_per_trackM = 0;
    percomBlock.sectors_per_trackL = 26;
    percomBlock.density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 720 && sectorSize == 256) // 5.25" SS/DD
  {
    percomBlock.density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 1440) // 5.25" DS/DD
  {
    percomBlock.num_sides = 1;
    percomBlock.density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 2880) // 5.25" DS/QD
  {
    percomBlock.num_sides = 1;
    percomBlock.num_tracks = 80;
    percomBlock.density = 4; // 1050 density is MFM, override.
  }
  else if (numSectors == 2002 && sectorSize == 128) // SS/SD 8"
  {
    percomBlock.num_tracks = 77;
    percomBlock.density = 0; // FM density
  }
  else if (numSectors == 2002 && sectorSize == 256) // SS/DD 8"
  {
    percomBlock.num_tracks = 77;
    percomBlock.density = 4; // MFM density
  }
  else if (numSectors == 4004 && sectorSize == 128) // DS/SD 8"
  {
    percomBlock.num_tracks = 77;
    percomBlock.density = 0; // FM density
  }
  else if (numSectors == 4004 && sectorSize == 256) // DS/DD 8"
  {
    percomBlock.num_sides = 1;
    percomBlock.num_tracks = 77;
    percomBlock.density = 4; // MFM density
  }
  else if (numSectors == 5760) // 1.44MB 3.5" High Density
  {
    percomBlock.num_sides = 1;
    percomBlock.num_tracks = 80;
    percomBlock.sectors_per_trackL = 36;
    percomBlock.density = 8; // I think this is right.
  }
  else
  {
    // This is a custom size, one long track.
    percomBlock.num_tracks = 1;
    percomBlock.sectors_per_trackM = numSectors >> 8;
    percomBlock.sectors_per_trackL = numSectors & 0xFF;
  }

#ifdef DEBUG_VERBOSE
  Debug_printf("Percom block dump for newly mounted device slot %d\n", deviceSlot);
  dump_percom_block(deviceSlot);
#endif
}

/**
   Read percom block
*/
void sioDisk::sio_read_percom_block()
{
// unsigned char deviceSlot = cmdFrame.devic - 0x31;
#ifdef DEBUG_VERBOSE
  dump_percom_block();
#endif
  sio_to_computer((byte *)&percomBlock, 12, false);
  SIO_UART.flush();
}

/**
   Write percom block
*/
void sioDisk::sio_write_percom_block()
{
  // unsigned char deviceSlot = cmdFrame.devic - 0x31;
  sio_to_peripheral((byte *)&percomBlock, 12);
#ifdef DEBUG_VERBOSE
  dump_percom_block(deviceSlot);
#endif
  sio_complete();
}

/**
   Dump PERCOM block
*/
void sioDisk::dump_percom_block()
{
#ifdef DEBUG_VERBOSE
  Debug_printf("Percom Block Dump\n");
  Debug_printf("-----------------\n");
  Debug_printf("Num Tracks: %d\n", percomBlock.num_tracks);
  Debug_printf("Step Rate: %d\n", percomBlock.step_rate);
  Debug_printf("Sectors per Track: %d\n", (percomBlock.sectors_per_trackM * 256 + percomBlock.sectors_per_trackL));
  Debug_printf("Num Sides: %d\n", percomBlock.num_sides);
  Debug_printf("Density: %d\n", percomBlock.density);
  Debug_printf("Sector Size: %d\n", (percomBlock.sector_sizeM * 256 + percomBlock.sector_sizeL));
  Debug_printf("Drive Present: %d\n", percomBlock.drive_present);
  Debug_printf("Reserved1: %d\n", percomBlock.reserved1);
  Debug_printf("Reserved2: %d\n", percomBlock.reserved2);
  Debug_printf("Reserved3: %d\n", percomBlock.reserved3);
#endif
}

/**
   (disk) High Speed
*/
void sioDisk::sio_high_speed()
{
  byte hsd = HISPEED_INDEX;
  sio_to_computer((byte *)&hsd, 1, false);
}

// Process command
void sioDisk::sio_process()
{
  if (_file == nullptr) // if there is no disk mounted, just return cuz there's nothing to do
    return;

  switch (cmdFrame.comnd)
  {
  case 'R':
    sio_ack();
    sio_read();
    break;
  case 'W':
  case 'P':
    sio_ack();
    sio_write();
    break;
  case 'S':
    sio_ack();
    sio_status();
    break;
  case '!':
  case '"':
    sio_ack();
    sio_format();
    break;
  case 0x4E:
    sio_ack();
    sio_read_percom_block();
    break;
  case 0x4F:
    sio_ack();
    sio_write_percom_block();
    break;
  case 0x3F:
    sio_ack();
    sio_high_speed();
    break;
  default:
    sio_nak();
  }
}

// mount a disk file
void sioDisk::mount(File *f)
{
  unsigned short newss;
  unsigned short num_para;
  unsigned char num_para_hi;
  unsigned short num_sectors;
  byte buf[2];

#ifdef DEBUG
#endif

  // Get file and sector size from header
  f->seek(2);      //tnfs_seek(deviceSlot, 2);
  f->read(buf, 2); //tnfs_read(deviceSlot, 2);
  num_para = (256 * buf[1]) + buf[0];
  f->read(buf, 2); //tnfs_read(deviceSlot, 2);
  newss = (256 * buf[1]) + buf[0];
  f->read(buf, 1); //tnfs_read(deviceSlot, 1);
  num_para_hi = buf[0];
  sectorSize = newss;
  num_sectors = para_to_num_sectors(num_para, num_para_hi, newss);
  derive_percom_block(num_sectors);
  _file = f;

#ifdef DEBUG
  Debug_print("mounting ATR to Disk: ");
  Debug_println(f->name());
  Debug_printf("num_para: %d\n", num_para);
  Debug_printf("sectorSize: %d\n", newss);
  Debug_printf("num_sectors: %d\n", num_sectors);
  Debug_println("mounted.");
#endif
}

// Invalidate disk cache
void sioDisk::invalidate_cache()
{
  firstCachedSector = 65535;
}

// mount a disk file
void sioDisk::umount()
{
  if (_file != nullptr)
  {
    _file->close();
    _file = nullptr;
  }
}

bool sioDisk::write_blank_atr(File *f, unsigned short sectorSize, unsigned short numSectors)
{
  union {
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

  unsigned long num_para = num_sectors_to_para(numSectors, sectorSize);
  unsigned long offset = 0;

  // Write header
  atrHeader.magic1 = 0x96;
  atrHeader.magic2 = 0x02;
  atrHeader.filesizeH = num_para & 0xFF;
  atrHeader.filesizeL = (num_para & 0xFF00) >> 8;
  atrHeader.filesizeHH = (num_para & 0xFF0000) >> 16;
  atrHeader.secsizeH = sectorSize & 0xFF;
  atrHeader.secsizeL = sectorSize >> 8;

#ifdef DEBUG
  Debug_printf("Write header\n");
#endif
  //memcpy(sector, atrHeader.rawData, sizeof(atrHeader.rawData));
  offset += f->write(atrHeader.rawData, sizeof(atrHeader.rawData));
  //tnfs_write(deviceSlot, sizeof(atrHeader.rawData));
  //offset += sizeof(atrHeader.rawData);

  // Write first three 128 byte sectors
  memset(sector, 0x00, sizeof(sector));

#ifdef DEBUG
  Debug_printf("Write first three sectors\n");
#endif

  for (unsigned char i = 0; i < 3; i++)
  {
    //tnfs_write(deviceSlot, 128);
    size_t out = f->write(sector, 128);
    if (out != 128)
    {
#ifdef DEBUG
      Debug_printf("Error writing sector %hhu\n", i);
#endif
      return false;
    }
    offset += 128;
    numSectors--;
  }

#ifdef DEBUG
  Debug_printf("Sparse Write the rest.\n");
#endif
  // Write the rest of the sectors via sparse seek
  offset += (numSectors * sectorSize) - sectorSize;
  //tnfs_seek(deviceSlot, offset);
  //tnfs_write(deviceSlot, sectorSize);
  f->seek(offset);
  size_t out = f->write(sector, sectorSize);
  if (out != sectorSize)
  {
#ifdef DEBUG
    Debug_println("Error writing last sector");
#endif
    return false;
  }
  return true; //fixme - JP fixed?
}

File *sioDisk::file()
{
  return _file;
}
