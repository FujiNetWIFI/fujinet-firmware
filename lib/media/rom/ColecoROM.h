#ifndef COLECOROM_H
#define COLECOROM_H

#include "ROMMedia.h"

#include <optional>
#include <string>
#include <memory_resource>
#include <vector>

class ColecoROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    ColecoROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}

    std::optional<std::pmr::vector<uint8_t>> mapperInfo() override;

private:
    std::optional<std::pmr::vector<uint8_t>> _mapperInfo;
};

#endif /* COLECOROM_H */
