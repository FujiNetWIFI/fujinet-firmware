#ifndef DISK_H
#define DISK_H

#include "FujiDiskDrive.h"
#include "mediatype.h"
#include "MediaFormats.h"
#include "disk/CoCoDSK.h"
#include "disk/DragonVDK.h"
#include "disk/PMD85MRM.h"
#include "disk/CoCoCAS.h"
#include "rom/CoCoCartROM.h"

#include <memory>

class drivewireDisk : public FujiDiskDrive
{
public:
    // Disks are tried before cartridges; within each, in this order.
    using Formats = MediaFormats<MEDIATYPE_UNKNOWN,
                                 MediaFormat<CoCoDSK, MEDIATYPE_DSK>,
                                 MediaFormat<DragonVDK, MEDIATYPE_VDK>,
                                 MediaFormat<PMD85MRM, MEDIATYPE_MRM>,
                                 MediaFormat<CoCoCAS, MEDIATYPE_CAS>,
                                 MediaFormat<CoCoCartROM, MEDIATYPE_ROM>>;

private:
    uint8_t _status = DISK_STATUS_OK;

public:
    // What get_media_status() reports: a good read, one past the end, and no media.
    static constexpr uint8_t DISK_STATUS_OK = 0;
    static constexpr uint8_t DISK_STATUS_EOF = 2;
    static constexpr uint8_t DISK_STATUS_NO_MEDIA = 3;

    /**
     * @brief Is this virtualDevice holding the virtual disk drive used to boot CONFIG?
     */
    bool is_config_device = false;

    drivewireDisk();
    ~drivewireDisk();

    bool isMounted() const override { return _media != nullptr; }

    // host lets a format reopen the image, as CoCoDSK does for high scores; may be nullptr.
    error_is_true mount(fnFile *f, const char *filename, uint32_t disksize,
                        disk_access_flags_t access_mode, fujiHost *host = nullptr);
    void unmount();

    struct NewDiskRequest {
        uint8_t numDisks;
        uint8_t hostSlot;
        uint8_t deviceSlot;
        char filename[MAX_FILENAME_LEN];
    } __attribute__((packed));
    success_is_true write_blank(fnFile *f, const NewDiskRequest &req);

    error_is_true read(uint32_t sector, ByteBuffer &data);
    error_is_true write(uint32_t sector, const ByteBuffer &data);

    uint8_t get_media_status();
};

#endif
