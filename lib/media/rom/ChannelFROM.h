#ifndef CHANNELFROM_H
#define CHANNELFROM_H

#include "ROMMedia.h"

class ChannelFROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    static constexpr const char *extensions[] = {".chf"};

    ChannelFROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* CHANNELFROM_H */
