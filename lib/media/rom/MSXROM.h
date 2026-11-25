#ifndef MSXROM_H
#define MSXROM_H

#include "ROMMedia.h"

#include <optional>
#include <string>
#include <vector>

class MSXROM : public ROMMedia
{
public:
    // Plain is a fixed image placed by its size; PlainPage0 and PlainPage2 are fixed
    // images that start at that page.
    enum class Mapper { Plain, PlainPage0, PlainPage2, Ascii8, Ascii16, Konami, KonamiScc };

protected:
    error_is_true attach() override;

public:
    static constexpr const char *extensions[] = {".rom", ".mx1", ".mx2"};

    MSXROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}

    size_result_t read(ByteBuffer &buffer, size_t length) override;
    std::optional<MapperLayout> mapperLayout() override;

private:
    void lookupDatabase();

    Mapper _mapper = Mapper::Plain;
    bool _tagged = false;
    bool _looked = false;
    std::vector<uint8_t> _image;
};

#endif /* MSXROM_H */
