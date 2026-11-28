#ifndef MSXDISKIMAGE_H
#define MSXDISKIMAGE_H

#include <cstdint>
#include <vector>

#include "media.h"

// One file to place in the root directory of a new image.
struct MsxDiskFile
{
    const char *name; // 11 characters, space padded: "MSXDOS  SYS"
    std::vector<uint8_t> data;
};

// Makes an MSX-DOS-format FAT12 floppy image: 360K (1DD) or 720K (2DD) with
// the given files in the root directory. It boots when the files include a
// system file the boot sector loads. The boot sector's BPB is always written
// because the rs232 bus can't format a disk, and RawDisk::geometryFromBootSector
// needs it to see 1DD as 80x1x9 rather than a 40x2 PC floppy.
class MsxDiskImage
{
public:
    static constexpr uint16_t SECTOR_SIZE = 512;
    static constexpr uint16_t BLOCKS_360K = 720;
    static constexpr uint16_t BLOCKS_720K = 1440;

    // Writes numBlocks sectors to f, starting from a copy of bootSector.
    static success_is_true create(fnFile *f, uint32_t numBlocks,
                                  const uint8_t (&bootSector)[SECTOR_SIZE],
                                  const std::vector<MsxDiskFile> &files, uint32_t volumeSerial,
                                  uint16_t dosDate, uint16_t dosTime);
};

#endif
