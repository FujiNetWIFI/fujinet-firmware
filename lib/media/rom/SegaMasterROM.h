#ifndef SEGAMASTERROM_H
#define SEGAMASTERROM_H

#include "ROMMedia.h"

#include <optional>
#include <string>
#include <memory_resource>
#include <vector>

class SegaMasterROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    SegaMasterROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}

    std::optional<std::pmr::vector<uint8_t>> mapperInfo() override;

private:
    std::optional<std::pmr::vector<uint8_t>> _mapperInfo;
};

#endif /* SEGAMASTERROM_H */
