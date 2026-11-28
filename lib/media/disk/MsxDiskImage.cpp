#include "MsxDiskImage.h"

#include <algorithm>
#include <cstring>

namespace
{

constexpr uint8_t SECTORS_PER_CLUSTER = 2;
constexpr uint16_t CLUSTER_SIZE = MsxDiskImage::SECTOR_SIZE * SECTORS_PER_CLUSTER;
constexpr uint8_t FAT_COUNT = 2;
constexpr uint16_t ROOT_ENTRIES = 112;
constexpr uint8_t DIR_ENTRY_SIZE = 32;
constexpr uint16_t ROOT_SECTORS = ROOT_ENTRIES * DIR_ENTRY_SIZE / MsxDiskImage::SECTOR_SIZE;
constexpr uint8_t SECTORS_PER_TRACK = 9;
constexpr uint16_t FIRST_DATA_CLUSTER = 2;
constexpr uint16_t FAT_END_OF_CHAIN = 0xFFF;
constexpr uint8_t ATTR_ARCHIVE = 0x20;
constexpr uint8_t BOOT_JUMP_SHORT = 0xEB;
constexpr uint8_t BOOT_JUMP_NEAR = 0xE9;

// Where the volume ID and serial sit in the Nextor / MSX-DOS 2 boot sector.
constexpr size_t BOOT_VOLUME_ID_OFFSET = 0x20;
constexpr char BOOT_VOLUME_ID[6] = {'V', 'O', 'L', '_', 'I', 'D'};
constexpr size_t BOOT_SERIAL_OFFSET = 0x27;

// The BIOS parameter block from the start of the boot sector.
struct BootSectorBpb
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
    u16le_t hiddenSectors;
} __attribute__((packed));
static_assert(sizeof(BootSectorBpb) == 0x1E, "BootSectorBpb must end at 0x1E");

struct DirectoryEntry
{
    char name[11];
    uint8_t attributes;
    uint8_t reserved[10];
    u16le_t time;
    u16le_t date;
    u16le_t startCluster;
    u32le_t size;
} __attribute__((packed));
static_assert(sizeof(DirectoryEntry) == DIR_ENTRY_SIZE, "DirectoryEntry must be 32 bytes");

struct Geometry
{
    uint8_t media;
    uint8_t heads;
    uint8_t sectorsPerFAT;
};

success_is_true geometryFor(uint32_t numBlocks, Geometry &geometry)
{
    switch (numBlocks)
    {
    case MsxDiskImage::BLOCKS_360K:
        geometry = {0xF8, 1, 2};
        RETURN_SUCCESS_AS_TRUE();
    case MsxDiskImage::BLOCKS_720K:
        geometry = {0xF9, 2, 3};
        RETURN_SUCCESS_AS_TRUE();
    default:
        RETURN_ERROR_AS_FALSE();
    }
}

void set_fat_entry(std::vector<uint8_t> &fat, uint16_t cluster, uint16_t value)
{
    size_t offset = cluster * 3 / 2;
    if (cluster % 2 == 0)
    {
        fat[offset] = value & 0xFF;
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | (value >> 8);
    }
    else
    {
        fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4);
        fat[offset + 1] = value >> 4;
    }
}

size_t clusters_for(size_t bytes)
{
    return (bytes + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
}

success_is_true write_all(fnFile *f, const void *data, size_t length)
{
    RETURN_SUCCESS_IF(length == 0 || fnio::fwrite(data, 1, length, f) == length);
}

success_is_true write_zeros(fnFile *f, size_t length)
{
    const uint8_t zeros[MsxDiskImage::SECTOR_SIZE] = {0};
    while (length > 0)
    {
        size_t chunk = std::min(length, sizeof(zeros));
        if (write_all(f, zeros, chunk).is_error())
            RETURN_ERROR_AS_FALSE();
        length -= chunk;
    }
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true bootSectorUsable(const uint8_t (&bootSector)[MsxDiskImage::SECTOR_SIZE])
{
    RETURN_SUCCESS_IF((bootSector[0] == BOOT_JUMP_SHORT || bootSector[0] == BOOT_JUMP_NEAR)
                      && memcmp(bootSector + BOOT_VOLUME_ID_OFFSET, BOOT_VOLUME_ID,
                                sizeof(BOOT_VOLUME_ID))
                             == 0);
}

} // namespace

success_is_true MsxDiskImage::create(fnFile *f, uint32_t numBlocks,
                                     const uint8_t (&bootSector)[SECTOR_SIZE],
                                     const std::vector<MsxDiskFile> &files,
                                     uint32_t volumeSerial, uint16_t dosDate, uint16_t dosTime)
{
    Geometry geometry;
    if (geometryFor(numBlocks, geometry).is_error() || bootSectorUsable(bootSector).is_error()
        || files.size() > ROOT_ENTRIES)
        RETURN_ERROR_AS_FALSE();

    constexpr uint8_t reservedSectors = 1;
    uint32_t dataStart = reservedSectors + FAT_COUNT * geometry.sectorsPerFAT + ROOT_SECTORS;
    size_t clusterCount = (numBlocks - dataStart) / SECTORS_PER_CLUSTER;

    size_t fatBytes = geometry.sectorsPerFAT * SECTOR_SIZE;
    std::vector<uint8_t> fat(fatBytes, 0);
    std::vector<uint8_t> root(ROOT_SECTORS * SECTOR_SIZE, 0);
    set_fat_entry(fat, 0, 0xF00 | geometry.media);
    set_fat_entry(fat, 1, FAT_END_OF_CHAIN);

    uint16_t nextCluster = FIRST_DATA_CLUSTER;
    size_t dirIndex = 0;
    for (const MsxDiskFile &file : files)
    {
        size_t used = clusters_for(file.data.size());
        if (nextCluster - FIRST_DATA_CLUSTER + used > clusterCount)
            RETURN_ERROR_AS_FALSE();

        DirectoryEntry entry = {};
        memcpy(entry.name, file.name, sizeof(entry.name));
        entry.attributes = ATTR_ARCHIVE;
        entry.time = dosTime;
        entry.date = dosDate;
        entry.startCluster = used > 0 ? nextCluster : 0;
        entry.size = file.data.size();
        memcpy(root.data() + dirIndex++ * DIR_ENTRY_SIZE, &entry, sizeof(entry));

        for (size_t idx = 0; idx < used; idx++, nextCluster++)
            set_fat_entry(fat, nextCluster,
                          idx + 1 < used ? nextCluster + 1 : FAT_END_OF_CHAIN);
    }

    uint8_t boot[SECTOR_SIZE];
    memcpy(boot, bootSector, sizeof(boot));
    BootSectorBpb bpb;
    memcpy(&bpb, boot, sizeof(bpb));
    bpb.bytesPerSector = SECTOR_SIZE;
    bpb.sectorsPerCluster = SECTORS_PER_CLUSTER;
    bpb.reservedSectors = reservedSectors;
    bpb.fatCount = FAT_COUNT;
    bpb.rootEntries = ROOT_ENTRIES;
    bpb.totalSectors = numBlocks;
    bpb.mediaDescriptor = geometry.media;
    bpb.sectorsPerFAT = geometry.sectorsPerFAT;
    bpb.sectorsPerTrack = SECTORS_PER_TRACK;
    bpb.headCount = geometry.heads;
    bpb.hiddenSectors = 0;
    memcpy(boot, &bpb, sizeof(bpb));
    u32le_t serial;
    serial = volumeSerial;
    memcpy(boot + BOOT_SERIAL_OFFSET, &serial, sizeof(serial));

    if (write_all(f, boot, sizeof(boot)).is_error())
        RETURN_ERROR_AS_FALSE();
    for (uint8_t copy = 0; copy < FAT_COUNT; copy++)
        if (write_all(f, fat.data(), fat.size()).is_error())
            RETURN_ERROR_AS_FALSE();
    if (write_all(f, root.data(), root.size()).is_error())
        RETURN_ERROR_AS_FALSE();

    uint64_t written = static_cast<uint64_t>(dataStart) * SECTOR_SIZE;
    for (const MsxDiskFile &file : files)
    {
        size_t padding = clusters_for(file.data.size()) * CLUSTER_SIZE - file.data.size();
        if (write_all(f, file.data.data(), file.data.size()).is_error()
            || write_zeros(f, padding).is_error())
            RETURN_ERROR_AS_FALSE();
        written += file.data.size() + padding;
    }

    return write_zeros(f, static_cast<uint64_t>(numBlocks) * SECTOR_SIZE - written);
}
