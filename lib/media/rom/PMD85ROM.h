#ifndef PMD85ROM_H
#define PMD85ROM_H

#include "ROMMedia.h"

// A PMD 85 ROM module, .rmm or .mrm: up to 256 pages of 32K.
class PMD85ROM : public ROMMedia
{
protected:
    error_is_true attach() override;

public:
    static constexpr const char *extensions[] = {".rmm", ".mrm"};
    static constexpr size_t PAGE_SIZE = 32_kB;
    static constexpr size_t MAX_PAGES = 256;

    PMD85ROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}
};

#endif /* PMD85ROM_H */
