#ifndef NESROM_H
#define NESROM_H

#include "ROMMedia.h"

class NESROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    NESROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* NESROM_H */
