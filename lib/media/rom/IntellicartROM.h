#ifndef INTELLICARTROM_H
#define INTELLICARTROM_H

#include "ROMMedia.h"

class IntellicartROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    IntellicartROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* INTELLICARTROM_H */
