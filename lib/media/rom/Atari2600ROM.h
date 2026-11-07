#ifndef ATARI2600ROM_H
#define ATARI2600ROM_H

#include "ROMMedia.h"

class Atari2600ROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    Atari2600ROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* ATARI2600ROM_H */
