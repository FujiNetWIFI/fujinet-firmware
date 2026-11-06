#ifndef COCOCARTROM_H
#define COCOCARTROM_H

#include "ROMMedia.h"

class CoCoCartROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    CoCoCartROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* COCOCARTROM_H */
