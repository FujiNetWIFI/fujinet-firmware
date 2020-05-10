#include "../../include/debug.h"
#include "fnSystem.h"
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

    // Subtract bias for the first three sectors
    if (sector_size > 128)
        file_size -= (3 * 128);

    return file_size >> 4;
}

// Read
void sioDisk::sio_read()
{
    unsigned short sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
    unsigned long offset = sector_offset(sectorNum, sectorSize);
    unsigned long ss = sector_size(sectorNum, sectorSize);
    byte err = false;

    // Clear sector buffer
    memset(sector, 0, sizeof(sector));

    if (sectorNum <= UNCACHED_REGION)
    {
        if (sectorNum != (lastSectorNum + 1))
            err = !(_file->seek(offset));

        if (!err)
            err = (_file->read(sector, ss) != ss);
    }
    else // Cached
    {
        // implement caching.
    }

    // Send result to Atari
    sio_to_computer((byte *)&sector, ss, err);
    lastSectorNum = sectorNum;
}

// write for W & P commands
void sioDisk::sio_write(bool verify)
{
#ifdef DEBUG
    Debug_println("disk WRITE");
#endif
    unsigned short sectorNum = (cmdFrame.aux2 * 256) + cmdFrame.aux1;
    long offset = sector_offset(sectorNum, sectorSize);
    unsigned short ss = sector_size(sectorNum, sectorSize);
    byte ck;

    memset(sector, 0, sizeof(sector));

    ck = sio_to_peripheral(sector, ss);

    if (ck != sio_checksum(sector, ss))
    {
        sio_error();
        return;
    }

    if (sectorNum != (lastSectorNum + 1))
    {
        if (!_file->seek(offset))
        {
            sio_error();
            return;
        }
    }

    if (_file->write(sector, ss) != ss)
    {
        sio_error();
        lastSectorNum = 65535; // invalidate seek cache.
        return;
    }

    _file->flush();

    if (verify)
    {
        if (!_file->seek(offset))
        {
            sio_error();
            return;
        }

        if (_file->read(sector, ss) != ss)
        {
            sio_error();
            return;
        }

        if (sio_checksum(sector, ss) != ck)
        {
            sio_error();
            return;
        }
    }

    sio_complete();

    lastSectorNum = sectorNum;
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
#ifdef DEBUG
    Debug_println("disk FORMAT");
#endif
    // Populate bad sector map (no bad sectors)
    for (int i = 0; i < sectorSize; i++)
        sector[i] = 0;

    sector[0] = 0xFF; // no bad sectors.
    sector[1] = 0xFF;

    // Send to computer
    sio_to_computer((byte *)sector, sectorSize, false);

#ifdef DEBUG
    Debug_printf("We faked a format.\n");
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
    //SIO_UART.flush();
    fnUartSIO.flush();
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


// mount a disk file
void sioDisk::mount(File *f)
{
    unsigned short newss;
    unsigned short num_para;
    unsigned char num_para_hi;
    unsigned short num_sectors;
    byte buf[2];

#ifdef DEBUG
    Debug_println("disk MOUNT");
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
    lastSectorNum = 65535; // Invalidate seek cache.

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
    // firstCachedSector = 65535;
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
    Debug_printf("Write header to \"%s\"\n", f->name());
#endif

    offset = f->write(atrHeader.rawData, sizeof(atrHeader.rawData));

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

/**
 * Calculate sector size
 * sectorNum - Sector # (1-65535)
 * sectorSize - Sector Size (128 or 256)
 */
long sector_offset(unsigned short sectorNum, unsigned short sectorSize)
{
    long offset = sectorNum;

    offset *= sectorSize;
    offset -= sectorSize;
    offset += 16;
    sectorSize = (sectorNum <= 3 ? 128 : sectorSize);

    // Bias adjustment for 256 bytes
    if (sectorSize == 256)
        offset -= 384;

    return offset;
}

/**
 * Return sector size
 * sectorNum - Sector # (1-65535)
 * sectorSize - Sector Size (128 or 256)
 */
unsigned short sector_size(unsigned short sectorNum, unsigned short sectorSize)
{
    return (sectorNum <= 3 ? 128 : sectorSize);
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
    case 'P':
        sio_ack();
        sio_write(false);
        break;
    case 'S':
        sio_ack();
        sio_status();
        break;
    case 'W':
        sio_ack();
        sio_write(true);
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
