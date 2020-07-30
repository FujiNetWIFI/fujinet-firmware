#include "../../include/debug.h"
#include <memory.h>
#include <string.h>
#include "fnSystem.h"
#include "disk.h"
#include "../utils/utils.h"

#define SIO_DISKCMD_FORMAT 0x21
#define SIO_DISKCMD_FORMAT_MEDIUM 0x22
#define SIO_DISKCMD_PUT 0x50
#define SIO_DISKCMD_READ 0x52
#define SIO_DISKCMD_STATUS 0x53
#define SIO_DISKCMD_WRITE 0x57

#define SIO_DISKCMD_HSIO_INDEX 0x3F
#define SIO_DISKCMD_HSIO_FORMAT 0xA1
#define SIO_DISKCMD_HSIO_FORMAT_MEDIUM 0xA2
#define SIO_DISKCMD_HSIO_PUT 0xD0
#define SIO_DISKCMD_HSIO_READ 0xD2
#define SIO_DISKCMD_HSIO_STATUS 0xD3
#define SIO_DISKCMD_HSIO_WRITE 0xD7

#define SIO_DISKCMD_PERCOM_READ 0x4E
#define SIO_DISKCMD_PERCOM_WRITE 0x4F

#define ATR_MAGIC_HEADER 0x0296 // Sum of 'NICKATARI'

int command_frame_counter = 0;


// Returns the internal file handle
FILE *sioDisk::file()
{
    return _file;
}

// Returns byte offset of given sector number (1-based)
uint32_t sector_to_offset(uint16_t sectorNum, uint16_t sectorSize)
{
    uint32_t offset = 0;

    // This should always be true, but just so we don't end up with a negative...
    if (sectorNum > 0)
        offset = sectorSize * (sectorNum - 1);

    offset += 16; // Adjust for ATR header

    // Adjust for the fact that the first 3 sectors are always 128-bytes even on 256-byte disks
    if (sectorSize == 256 && sectorNum > 3)
        offset -= 384;

    return offset;
}

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t sector_size(uint16_t sectorNum, uint16_t sectorSize)
{
    return sectorNum <= 3 ? 128 : sectorSize;
}

// Read
void sioDisk::sio_read()
{
    Debug_print("disk READ\n");

    uint16_t sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
    uint32_t offset = sector_to_offset(sectorNum, _sectorSize);
    uint32_t ss = sector_size(sectorNum, _sectorSize);
    bool err = false;

    // Clear sector buffer
    memset(_sector, 0, sizeof(_sector));

    __BEGIN_IGNORE_TYPELIMITS
    if (sectorNum <= UNCACHED_REGION)
    {
        if (sectorNum != (_lastSectorNum + 1))
            err = fseek(_file, offset, SEEK_SET) != 0;

        if (!err)
            err = fread(_sector, 1, ss, _file) != ss;
    }
    else // Cached
    {
        // implement caching.
    }
    __END_IGNORE_TYPELIMITS

    // Send result to Atari
    sio_to_computer((uint8_t *)&_sector, ss, err);
    _lastSectorNum = sectorNum;
}

// write for W & P commands
void sioDisk::sio_write(bool verify)
{
    Debug_print("disk WRITE\n");

    uint16_t sectorNum = (cmdFrame.aux2 * 256) + cmdFrame.aux1;
    uint32_t offset = sector_to_offset(sectorNum, _sectorSize);
    uint16_t ss = sector_size(sectorNum, _sectorSize);
    uint8_t ck;

    memset(_sector, 0, sizeof(_sector));

    ck = sio_to_peripheral(_sector, ss);

    if (ck != sio_checksum(_sector, ss))
    {
        sio_error();
        return;
    }

    if (sectorNum != (_lastSectorNum + 1))
    {
        if (fseek(_file, offset, SEEK_SET) != 0)
        {
            sio_error();
            return;
        }
    }

    if (fwrite(_sector, 1, ss, _file) != ss)
    {
        sio_error();
        _lastSectorNum = 65535; // invalidate seek cache.
        return;
    }

    int ret = fflush(_file);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    ret = fsync(fileno(_file)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("sioDisk::sio_write fsync:%d\n", ret);

    if (verify)
    {
        if (fseek(_file, offset, SEEK_SET) != 0)
        {
            sio_error();
            return;
        }

        if (fread(_sector, 1, ss, _file) != ss)
        {
            sio_error();
            return;
        }

        if (sio_checksum(_sector, ss) != ck)
        {
            sio_error();
            return;
        }
    }

    Debug_println("disk WRITE completed");

    sio_complete();

    _lastSectorNum = sectorNum;
}

// Status
void sioDisk::sio_status()
{
    Debug_print("disk STATUS\n");

    uint8_t status[4] = {0x10, 0xDF, 0xFE, 0x00};

    if (_sectorSize == 256)
        status[0] |= 0x20;

    // todo:
    if (_percomBlock.sectors_per_trackL == 26)
        status[0] |= 0x80;

    sio_to_computer(status, sizeof(status), false);
}

// fake disk format
void sioDisk::sio_format()
{
    Debug_print("disk FORMAT\n");

    // Populate bad sector map (no bad sectors)
    memset(_sector, 0, _sectorSize);
    _sector[0] = 0xFF; // no bad sectors.
    _sector[1] = 0xFF;

    // Send to computer
    sio_to_computer(_sector, _sectorSize, false);

    Debug_println("we faked a format");
}

// Update PERCOM block from the total # of sectors
void sioDisk::derive_percom_block(uint16_t numSectors)
{
    // Start with 40T/1S 720 Sectors, sector size passed in
    _percomBlock.num_tracks = 40;
    _percomBlock.step_rate = 1;
    _percomBlock.sectors_per_trackM = 0;
    _percomBlock.sectors_per_trackL = 18;
    _percomBlock.num_sides = 0;
    _percomBlock.density = 0; // >128 bytes = MFM
    _percomBlock.sector_sizeM = (_sectorSize == 256 ? 0x01 : 0x00);
    _percomBlock.sector_sizeL = (_sectorSize == 256 ? 0x00 : 0x80);
    _percomBlock.drive_present = 255;
    _percomBlock.reserved1 = 0;
    _percomBlock.reserved2 = 0;
    _percomBlock.reserved3 = 0;

    if (numSectors == 1040) // 5.25" 1050 density
    {
        _percomBlock.sectors_per_trackM = 0;
        _percomBlock.sectors_per_trackL = 26;
        _percomBlock.density = 4; // 1050 density is MFM, override.
    }
    else if (numSectors == 720 && _sectorSize == 256) // 5.25" SS/DD
    {
        _percomBlock.density = 4; // 1050 density is MFM, override.
    }
    else if (numSectors == 1440) // 5.25" DS/DD
    {
        _percomBlock.num_sides = 1;
        _percomBlock.density = 4; // 1050 density is MFM, override.
    }
    else if (numSectors == 2880) // 5.25" DS/QD
    {
        _percomBlock.num_sides = 1;
        _percomBlock.num_tracks = 80;
        _percomBlock.density = 4; // 1050 density is MFM, override.
    }
    else if (numSectors == 2002 && _sectorSize == 128) // SS/SD 8"
    {
        _percomBlock.num_tracks = 77;
        _percomBlock.density = 0; // FM density
    }
    else if (numSectors == 2002 && _sectorSize == 256) // SS/DD 8"
    {
        _percomBlock.num_tracks = 77;
        _percomBlock.density = 4; // MFM density
    }
    else if (numSectors == 4004 && _sectorSize == 128) // DS/SD 8"
    {
        _percomBlock.num_tracks = 77;
        _percomBlock.density = 0; // FM density
    }
    else if (numSectors == 4004 && _sectorSize == 256) // DS/DD 8"
    {
        _percomBlock.num_sides = 1;
        _percomBlock.num_tracks = 77;
        _percomBlock.density = 4; // MFM density
    }
    else if (numSectors == 5760) // 1.44MB 3.5" High Density
    {
        _percomBlock.num_sides = 1;
        _percomBlock.num_tracks = 80;
        _percomBlock.sectors_per_trackL = 36;
        _percomBlock.density = 8; // I think this is right.
    }
    else
    {
        // This is a custom size, one long track.
        _percomBlock.num_tracks = 1;
        _percomBlock.sectors_per_trackM = numSectors >> 8;
        _percomBlock.sectors_per_trackL = numSectors & 0xFF;
    }

#ifdef VERBOSE_DISK
    Debug_printf("Percom block dump for newly mounted device slot %d\n", deviceSlot);
    dump_percom_block(deviceSlot);
#endif
}

// Read percom block
void sioDisk::sio_read_percom_block()
{
#ifdef VERBOSE_DISK
    dump_percom_block();
#endif
    sio_to_computer((uint8_t *)&_percomBlock, 12, false);

    fnUartSIO.flush();
}

// Write percom block
void sioDisk::sio_write_percom_block()
{
    sio_to_peripheral((uint8_t *)&_percomBlock, 12);
#ifdef VERBOSE_DISK
    dump_percom_block(deviceSlot);
#endif
    sio_complete();
}

// Dump PERCOM block
void sioDisk::dump_percom_block()
{
#ifdef VERBOSE_DISK
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

/* 
 Mount ATR disk
 Header layout:
 00 lobyte 0x96
 01 hibyte 0x02
 02 lobyte paragraphs (16-byte blocks) on disk
 03 hibyte
 04 lobyte sector size (0x80, 0x100, etc.)
 05 hibyte
 06   byte paragraphs on disk extension (24-bits total)
 
 07-0F have two possible interpretations but are no critical for our use
*/
void sioDisk::mount(FILE *f)
{
    Debug_print("disk MOUNT\n");

    uint16_t num_bytes_sector;
    uint32_t num_paragraphs;
    uint16_t num_sectors;
    uint8_t buf[7];

    // Get file and sector size from header

    if (fseek(f, 0, SEEK_SET) < 0)
    {
        Debug_println("failed seeking to header on disk image");
        return;
    }
    int i;
    if ((i = fread(buf, 1, sizeof(buf), f)) != sizeof(buf))
    {
        Debug_printf("failed reading header bytes (%d, %d)\n", i, errno);
        return;
    }
    // Check the magic number
    if (UINT16_FROM_HILOBYTES(buf[1], buf[0]) != ATR_MAGIC_HEADER)
    {
        Debug_println("ATR header missing 'NICKATARI'");
        return;
    }

    num_bytes_sector = UINT16_FROM_HILOBYTES(buf[5], buf[4]);

    num_paragraphs = UINT16_FROM_HILOBYTES(buf[3], buf[2]);
    num_paragraphs = num_paragraphs | (buf[6] << 16);

    _sectorSize = num_bytes_sector;

    num_sectors = (num_paragraphs * 16) / num_bytes_sector;
    // Adjust sector size for the fact that the first three sectors are *always* 128 bytes
    if (num_bytes_sector == 256)
        num_sectors += 2;

    derive_percom_block(num_sectors);

    _file = f;
    _lastSectorNum = 65535; // Invalidate seek cache.

    Debug_printf("mounted ATR: paragraphs=%hu, sect_size=%hu, sect_count=%hu\n",
                 num_paragraphs, num_bytes_sector, num_sectors);
}

// mount a disk file
void sioDisk::umount()
{
    Debug_print("disk UNMOUNT\n");

    if (_file != nullptr)
    {
        fclose(_file);
        _file = nullptr;
    }
}

bool sioDisk::write_blank_atr(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    union {
        struct
        {
            uint8_t magicL;
            uint8_t magicH;
            uint8_t filesizeL;
            uint8_t filesizeH;
            uint8_t secsizeL;
            uint8_t secsizeH;
            uint8_t filesizeHH;
            uint8_t res0;
            uint8_t res1;
            uint8_t res2;
            uint8_t res3;
            uint8_t res4;
            uint8_t res5;
            uint8_t res6;
            uint8_t res7;
            uint8_t flags;
        };
    } atrHeader;

    memset(&atrHeader, 0, sizeof(atrHeader));

    uint32_t total_size = numSectors * sectorSize;
    // Adjust for first 3 sectors always being single-density (we lose 384 bytes)
    if (sectorSize > 128)
        total_size -= 384; // 3 * 128

    uint32_t num_paragraphs = total_size / 16;

    // Write header
    atrHeader.magicL = LOBYTE_FROM_UINT16(ATR_MAGIC_HEADER);
    atrHeader.magicH = HIBYTE_FROM_UINT16(ATR_MAGIC_HEADER);

    atrHeader.filesizeL = LOBYTE_FROM_UINT16(num_paragraphs);
    atrHeader.filesizeH = HIBYTE_FROM_UINT16(num_paragraphs);
    atrHeader.filesizeHH = (num_paragraphs & 0xFF0000) >> 16;

    atrHeader.secsizeL = LOBYTE_FROM_UINT16(sectorSize);
    atrHeader.secsizeH = HIBYTE_FROM_UINT16(sectorSize);

    Debug_printf("Write header to ATR: sec_size=%hu, sectors=%hu, paragraphs=%hu\n");

    uint32_t offset = fwrite(&atrHeader, 1, sizeof(atrHeader), f);

    // Write first three 128 uint8_t sectors
    memset(_sector, 0, sizeof(_sector));

    for (int i = 0; i < 3; i++)
    {
        size_t out = fwrite(_sector, 1, 128, f);
        if (out != 128)
        {
            Debug_printf("Error writing sector %hhu\n", i);
            return false;
        }
        offset += 128;
        numSectors--;
    }

    // Write the rest of the sectors via sparse seek
    offset += (numSectors * sectorSize) - sectorSize;
    fseek(f, offset, SEEK_SET);
    size_t out = fwrite(_sector, 1, sectorSize, f);

    if (out != sectorSize)
    {
        Debug_println("Error writing last sector");
        return false;
    }

    return true;
}

// Process command
void sioDisk::sio_process()
{
    if (_file == nullptr) // If there is no disk mounted, just return cuz there's nothing to do
        return;

    if (device_active == false &&
        (cmdFrame.comnd != SIO_DISKCMD_STATUS && cmdFrame.comnd != SIO_DISKCMD_HSIO_INDEX))
        return;

    Debug_print("disk sio_process()\n");

    switch (cmdFrame.comnd)
    {
    case SIO_DISKCMD_READ:
    case SIO_DISKCMD_HSIO_READ:
        sio_ack();
        sio_read();
        break;
    case SIO_DISKCMD_PUT:
    case SIO_DISKCMD_HSIO_PUT:
        sio_ack();
        sio_write(false);
        break;
    case SIO_DISKCMD_STATUS:
    case SIO_DISKCMD_HSIO_STATUS:
        if (is_config_device == true)
        {
            if (status_wait_count == 0)
            {
                device_active = true;
                sio_ack();
                sio_status();
            }
            else
            {
                status_wait_count--;
            }
        }
        else
        {
            sio_ack();
            sio_status();
        }
        break;
    case SIO_DISKCMD_WRITE:
    case SIO_DISKCMD_HSIO_WRITE:
        sio_ack();
        sio_write(true);
        break;
    case SIO_DISKCMD_FORMAT:
    case SIO_DISKCMD_FORMAT_MEDIUM:
    case SIO_DISKCMD_HSIO_FORMAT:
    case SIO_DISKCMD_HSIO_FORMAT_MEDIUM:
        sio_ack();
        sio_format();
        break;
    case SIO_DISKCMD_PERCOM_READ:
        sio_ack();
        sio_read_percom_block();
        break;
    case SIO_DISKCMD_PERCOM_WRITE:
        sio_ack();
        sio_write_percom_block();
        break;
    case SIO_DISKCMD_HSIO_INDEX:
        sio_ack();
        sio_high_speed();
        break;
    default:
        sio_nak();
    }
}
