#include "RawDisk.h"

#include <algorithm>
#include <cstring>

// What a guess falls back to: the sectors per track of the most common PC floppies.
constexpr uint8_t RAW_DISK_DEFAULT_SECTORS_TRACK = 9;

// The start of a DOS 2.0 BIOS parameter block, all a floppy's geometry needs.
struct FATBootSector
{
    uint8_t jump[3];
    char oemName[8];
    u16le_t bytesPerSector;
    uint8_t sectorsPerCluster;
    u16le_t reservedSectors;
    uint8_t fatCount;
    u16le_t rootEntries;
    u16le_t totalSectors;
    uint8_t mediaDescriptor;
    u16le_t sectorsPerFAT;
    u16le_t sectorsPerTrack;
    u16le_t headCount;
} __attribute__((packed));

struct FloppyGeometry
{
    uint32_t blocks;
    uint16_t cylinders;
    uint8_t sides;
    uint8_t sectorsPerTrack;
};

// Every PC floppy format with 512-byte sectors.
static constexpr FloppyGeometry PC_FLOPPIES[] = {
    {320, 40, 1, 8},   {360, 40, 1, 9},   {640, 40, 2, 8},   {720, 40, 2, 9},
    {1440, 80, 2, 9},  {2400, 80, 2, 15}, {2880, 80, 2, 18}, {5760, 80, 2, 36},
};

uint16_t RawDisk::blockSize(uint32_t block) const
{
    return block < blockCount() ? RAW_DISK_SECTOR_SIZE : 0;
}

error_is_true RawDisk::blockToOffset(uint32_t block, uint32_t &offset) const
{
    if (block >= blockCount())
        RETURN_ERROR_AS_TRUE();
    offset = block * RAW_DISK_SECTOR_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

TrackGeometry RawDisk::trackGeometry(uint16_t cylinder, uint8_t side) const
{
    if (cylinder >= _cylinders || side >= _sides)
        return {0, 0};
    return {_sectorsPerTrack, RAW_DISK_SECTOR_SIZE, _encoding};
}

error_is_true RawDisk::attach()
{
    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    if (blockCount() > 0 && geometryFromBootSector().is_error()
        && geometryFromFloppySize().is_error())
        setGeometry(std::min<uint32_t>((blockCount() + RAW_DISK_DEFAULT_SECTORS_TRACK - 1)
                                           / RAW_DISK_DEFAULT_SECTORS_TRACK,
                                       UINT16_MAX),
                    1, RAW_DISK_DEFAULT_SECTORS_TRACK, TrackEncoding::UNKNOWN);
    RETURN_SUCCESS_AS_FALSE();
}

// Trusted only when it describes 512-byte sectors and exactly this many of them.
success_is_true RawDisk::geometryFromBootSector()
{
    ByteBuffer sector;
    if (readBlock(0, sector).is_error())
        RETURN_ERROR_AS_FALSE();

    FATBootSector boot;
    memcpy(&boot, sector.data(), sizeof(boot));
    uint16_t sectorsPerTrack = boot.sectorsPerTrack;
    uint16_t heads = boot.headCount;
    if ((boot.jump[0] != 0xEB && boot.jump[0] != 0xE9)
        || boot.bytesPerSector != RAW_DISK_SECTOR_SIZE || boot.totalSectors != blockCount()
        || sectorsPerTrack == 0 || sectorsPerTrack > UINT8_MAX || heads == 0 || heads > 2
        || blockCount() % (sectorsPerTrack * heads) != 0)
        RETURN_ERROR_AS_FALSE();

    setGeometry(blockCount() / (sectorsPerTrack * heads), heads, sectorsPerTrack,
                TrackEncoding::MFM);
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true RawDisk::geometryFromFloppySize()
{
    for (const FloppyGeometry &floppy : PC_FLOPPIES)
    {
        if (floppy.blocks == blockCount())
        {
            setGeometry(floppy.cylinders, floppy.sides, floppy.sectorsPerTrack,
                        TrackEncoding::MFM);
            RETURN_SUCCESS_AS_TRUE();
        }
    }
    RETURN_ERROR_AS_FALSE();
}

void RawDisk::setGeometry(uint16_t cylinders, uint8_t sides, uint8_t sectorsPerTrack,
                           TrackEncoding encoding)
{
    _cylinders = cylinders;
    _sides = sides;
    _sectorsPerTrack = sectorsPerTrack;
    _encoding = encoding;
}

error_is_true RawDisk::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    if (BlockMedia::writeBlock(block, buffer).is_error())
        RETURN_ERROR_AS_TRUE();
    RETURN_ERROR_IF(fnio::fflush(_fileh) != 0);
}

success_is_true RawDisk::create(fnFile *f, uint16_t sectorSize, uint32_t numSectors)
{
    uint8_t blank[RAW_DISK_SECTOR_SIZE] = {0};
    uint64_t remaining = (uint64_t)sectorSize * numSectors;
    if (remaining == 0)
        RETURN_ERROR_AS_FALSE();

    while (remaining > 0)
    {
        size_t length = std::min<uint64_t>(remaining, sizeof(blank));
        if (fnio::fwrite(blank, 1, length, f) != length)
            RETURN_ERROR_AS_FALSE();
        remaining -= length;
    }
    RETURN_SUCCESS_AS_TRUE();
}
