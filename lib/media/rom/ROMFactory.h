#ifndef ROMFACTORY_H
#define ROMFACTORY_H

#include "ROMMedia.h"
#include "CoCoCartROM.h"

#include <memory>
#include <string>

class ROMFactory
{
public:
    ROMFactory() = delete;

    // Tries each of Formats in the order listed and returns the first whose
    // attach() accepts the file, or nullptr. A rejected format leaves handle
    // open, so a caller can try another list with the same handle.
    template <typename... Formats>
    static std::unique_ptr<ROMMedia> open(const std::string &path, fnFile *handle)
    {
        static_assert(sizeof...(Formats) > 0, "ROMFactory::open needs at least one format");
        std::unique_ptr<ROMMedia> media;
        ((media = Media::open<Formats>(path, handle)) || ...);
        return media;
    }
};

#endif /* ROMFACTORY_H */
