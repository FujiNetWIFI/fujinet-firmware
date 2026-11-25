#ifndef DISK_H
#define DISK_H

#include "FujiDiskDrive.h"
#include "mediatype.h"
#include "MediaFormats.h"

#include "disk/AltairDSK.h"
#include "disk/IMDDisk.h"
#include "disk/JV1Disk.h"
#include "disk/RawDisk.h"

#include "rom/Atari2600ROM.h"
#include "rom/Atari5200ROM.h"
#include "rom/Atari7800ROM.h"
#include "rom/ChannelFROM.h"
#include "rom/ColecoROM.h"
#include "rom/IntellicartROM.h"
#include "rom/IntellivisionROM.h"
#include "rom/MSXROM.h"
#include "rom/NESROM.h"
#include "rom/SegaMasterROM.h"
#include "rom/StudioIIROM.h"

#include <ctime>
#include <memory>

class fujiHost;

class rs232Disk : public FujiDiskDrive
{
public:
    // Cartridges are tried first, strongest evidence first: a signature, a
    // mapper file, then the extension alone. Then the disks: an IMD header, a
    // JV1 size, an Altair size, and raw for anything else. A name alone can't
    // tell an Altair .dsk, so it lists as unknown.
    using Formats = MediaFormats<MEDIATYPE_UNKNOWN,
                                 MediaFormat<IntellicartROM, MEDIATYPE_ROM>,
                                 MediaFormat<NESROM, MEDIATYPE_ROM>,
                                 MediaFormat<MSXROM, MEDIATYPE_ROM>,
                                 MediaFormat<IntellivisionROM, MEDIATYPE_ROM>,
                                 MediaFormat<Atari7800ROM, MEDIATYPE_ROM>,
                                 MediaFormat<StudioIIROM, MEDIATYPE_ROM>,
                                 MediaFormat<Atari5200ROM, MEDIATYPE_ROM>,
                                 MediaFormat<Atari2600ROM, MEDIATYPE_ROM>,
                                 MediaFormat<SegaMasterROM, MEDIATYPE_ROM>,
                                 MediaFormat<ChannelFROM, MEDIATYPE_ROM>,
                                 MediaFormat<ColecoROM, MEDIATYPE_ROM>,
                                 MediaFormat<IMDDisk, MEDIATYPE_IMD>,
                                 MediaFormat<JV1Disk, MEDIATYPE_JV1>,
                                 MediaFormat<AltairDSK, MEDIATYPE_UNKNOWN>,
                                 MediaFormat<RawDisk, MEDIATYPE_IMG>>;

    // A ROM goes to the DBC in blocks of this size.
    static constexpr size_t ROM_BLOCK_SIZE = 512;

    // The DISK_STATUS bits still reported, as an Atari 810 or XF551 would.
    static constexpr uint8_t DRIVE_STATUS_WRITE_PROTECT = 0x08;
    static constexpr uint8_t DRIVE_STATUS_DOUBLE_DENSITY = 0x20;
    static constexpr uint8_t DRIVE_STATUS_DOUBLE_SIDED = 0x40;
    static constexpr uint8_t DRIVE_STATUS_ENHANCED_DENSITY = 0x80;
    static constexpr uint8_t CONTROLLER_STATUS_WRITE_PROTECT = 0x40;
    static constexpr uint8_t PERCOM_DENSITY_FM = 0;
    static constexpr uint8_t PERCOM_DENSITY_MFM = 4;

private:
    // The disk's geometry as DISK_PERCOM_READ sends it.
    struct PercomBlock
    {
        uint8_t numTracks; // 0 for more than 255
        uint8_t stepRate;
        u16be_t sectorsPerTrack;
        uint8_t numSides; // 0 single, 1 double
        uint8_t density;
        u16be_t sectorSize;
        uint8_t drivePresent;
        uint8_t reserved[3];
    } __attribute__((packed));
    static_assert(sizeof(PercomBlock) == 12, "PercomBlock must be 12 bytes");

    time_t _mount_time = 0;

    void rs232_read(uint32_t sector);
    void rs232_write(uint32_t sector, bool verify);
    void rs232_format();
    void rs232_status(FujiStatusReq reqType);

    PercomBlock derivePercom();
    void rs232_read_percom_block();

protected:
    void rs232_process(const FujiBusPacket &packet) override;

public:
    rs232Disk();
    ~rs232Disk();

    // host lets a cartridge find its mapper file beside it; may be nullptr.
    error_is_true mount(fnFile *f, const char *filename, uint32_t disksize,
                        disk_access_flags_t access_mode, fujiHost *host = nullptr);
    void unmount();

    struct NewDiskRequest {
        u16le_t numSectors;
        u16le_t sectorSize;
        uint8_t hostSlot;
        uint8_t deviceSlot;
        char filename[MAX_FILENAME_LEN];
    } __attribute__((packed));
    success_is_true write_blank(fnFile *f, const NewDiskRequest &req);

    time_t mount_time() { return _mount_time; }
};

#endif
