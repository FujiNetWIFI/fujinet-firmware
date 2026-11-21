#ifndef STUDIOIIROM_H
#define STUDIOIIROM_H

#include "ROMMedia.h"

class StudioIIROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    static constexpr const char *extensions[] = {".st2"};

    StudioIIROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* STUDIOIIROM_H */
