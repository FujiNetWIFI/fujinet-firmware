#ifndef MSXROM_H
#define MSXROM_H

#include "ROMMedia.h"

#include <optional>
#include <string>

class MSXROM : public ROMMedia
{
public:
    enum class Mapper { Plain, Ascii8, Ascii16, Konami, KonamiScc };

protected:
    error_is_true attach() override;

public:
    static constexpr const char *extensions[] = {".rom", ".mx1", ".mx2"};

    MSXROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}

    std::optional<MapperLayout> mapperLayout() override;

private:
    Mapper _mapper = Mapper::Plain;
};

#endif /* MSXROM_H */
