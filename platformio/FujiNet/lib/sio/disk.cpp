#include "disk.h"

// Read
void sioDisk::sio_read()
{
  int ss;
  int sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  int cacheOffset = 0;
  int offset;
  byte *s;
  byte *d;
  byte err = false;

  max_cached_sectors = (sectorSize == 256 ? 9 : 19);
  if ((sectorNum > (firstCachedSector + max_cached_sectors)) || (sectorNum < firstCachedSector)) // cache miss
  {
    firstCachedSector = sectorNum;
    cacheOffset = 0;

    if (sectorNum < 4)
      ss = 128; // First three sectors are always single density
    else
      ss = sectorSize;

    offset = sectorNum;
    offset *= ss;
    offset -= ss;

    // Bias adjustment for 256 bytes
    if (ss == 256)
      offset -= 384;

    offset += 16;

#ifdef DEBUG_VERBOSE
    Debug_printf("firstCachedSector: %d\n", firstCachedSector);
    Debug_printf("cacheOffset: %d\n", cacheOffset);
    Debug_printf("offset: %d\n", offset);
#endif

    _file->seek(offset); //tnfs_seek(deviceSlot, offset);

    for (unsigned char i = 0; i < 10; i++)
    {
      _file->read(sector, 256);
      s = &sector[0]; // &tnfsPacket.data[3];
      d = &sectorCache[cacheOffset];
      memcpy(d, s, 256);
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

    cacheOffset = ((sectorNum - firstCachedSector) * ss);
#ifdef DEBUG_VERBOSE
    Debug_printf("cacheOffset: %d\n", cacheOffset);
#endif
  }
  d = &sector[0];
  s = &sectorCache[cacheOffset];
  memcpy(d, s, ss);
 
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
    // todo:
    // if (load_config == true)
    // {
    //   atrConfig.seek(offset, SeekSet);
    //   atrConfig.write(sector, ss);
    //   atrConfig.flush();
    // }
    // else
    //{
    _file->seek(offset);      // tnfs_seek(deviceSlot, offset);
    _file->write(sector, ss); // tnfs_write(deviceSlot, ss);
    _file->flush();
    // todo: firstCachedSector[cmdFrame.devic - 0x31] = 65535; // invalidate cache
    //}
    sio_complete();
  }
  else
  {
    sio_error();
  }
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
void sioDisk::derive_percom_block(unsigned short sectorSize, unsigned short numSectors)
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

// ****************************************************************************************

// Process command
void sioDisk::sio_process()
{
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
  default:
    sio_nak();
  }
}

// mount a disk file
void sioDisk::mount(File *f)
{
  _file = f;
}

File *sioDisk::file()
{
  return _file;
}