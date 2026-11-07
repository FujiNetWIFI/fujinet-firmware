#ifndef ATARI7800ROM_H
#define ATARI7800ROM_H

#include "ROMMedia.h"

#include <optional>
#include <string>
#include <memory_resource>
#include <vector>

class Atari7800ROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    Atari7800ROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}

    std::optional<std::pmr::vector<uint8_t>> mapperInfo() override;

private:
    std::optional<std::pmr::vector<uint8_t>> _mapperInfo;
};

#endif /* ATARI7800ROM_H */
