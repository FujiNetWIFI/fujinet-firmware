#ifndef FUJIDISKDRIVE_H
#define FUJIDISKDRIVE_H

#include "bus.h"
#include "media.h"

#define MAX_FILENAME_LEN 256

enum disk_access_flags_t {
    DISK_ACCESS_MODE_INVALID = 0x00,
    DISK_ACCESS_MODE_READ    = 0x01,
    DISK_ACCESS_MODE_WRITE   = 0x02,
    DISK_ACCESS_MODE_MOUNTED = 0x40,
};

// The contract every bus's disk drive implements; the authority on what it has mounted.
class FujiDiskDrive : public DiskDriveBase
{
protected:
    std::unique_ptr<DiskDriveMedia> _media;

public:
    using DiskDriveBase::DiskDriveBase;

    virtual bool isMounted() const { return _media != nullptr; }
};

#endif /* FUJIDISKDRIVE_H */
