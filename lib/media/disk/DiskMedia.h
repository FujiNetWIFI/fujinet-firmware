#ifndef _DISK_MEDIA_
#define _DISK_MEDIA_

#include <cstdint>

#include "media.h"

class DiskMedia : public Media
{
public:
    DiskMedia(const std::string &path, fnFile *handle) : Media(path, handle) {}

    virtual uint8_t sideCount() const = 0;
    virtual uint16_t cylinderCount() const = 0;

    error_is_true close() override;

protected:
    size_t _size = 0;

    // Sets _size from the file; an error if the length can't be read.
    error_is_true readSize();
};

#endif // _DISK_MEDIA_
