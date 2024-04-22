#ifdef BUILD_RC2014

#include "mediaType.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

const std::vector<MediaType::DiskImageDetails> supported_images =
{{
    // 8MB Hard Disk Drive
    { MEDIATYPE_IMG_HD,           "MEDIATYPE_IMG_HD", "IMG", (8 * 1024 * 1024)      },
    { MEDIATYPE_IMG_HD,           "MEDIATYPE_IMG_HD", "CPM", (8 * 1024 * 1024)      },

    // 3.5" DS/DD Floppy Drive (720K)
    { MEDIATYPE_IMG_FD720,        "MEDIATYPE_IMG_FD720", "IMG", (80 * 2 * 9 * 512)  },

    // The PCW256/Pro-DOS definition is listed here;
    { MEDIATYPE_IMG_FD720_PCW256, "MEDIATYPE_IMG_FD720_PCW256", "DSK", (80 * 2 * 9 * 512) },

    // 3.5" DS/HD Floppy Drive (1.44M)
    { MEDIATYPE_IMG_FD144,        "MEDIATYPE_IMG_FD144", "IMG", (80 * 2 * 18 * 512) },

    // 5.25" DS/DD Floppy Drive (360K) 
    { MEDIATYPE_IMG_FD360,        "MEDIATYPE_IMG_FD360", "IMG", (40 * 2 * 9 * 512)  },

    // 5.25" DS/HD Floppy Drive (1.2M)
    { MEDIATYPE_IMG_FD120,        "MEDIATYPE_IMG_FD120", "IMG", (80 * 2 * 15 * 512) },

    // 8" DS/DD Floppy Drive (1.11M)
    { MEDIATYPE_IMG_FD111,        "MEDIATYPE_IMG_FD120", "IMG", (77 * 2 * 15 * 512) },

}};

MediaType::~MediaType()
{
    unmount();
}

bool MediaType::format(uint16_t *responsesize)
{
    return true;
}

bool MediaType::read(uint16_t sectornum, uint16_t *readcount)
{
    return true;
}

bool MediaType::write(uint16_t sectornum, bool verify)
{
    return true;
}

void MediaType::unmount()
{
    if (_media_fileh != nullptr)
    {
        fclose(_media_fileh);
        _media_fileh = nullptr;
    }
}

mediatype_t MediaType::discover_mediatype(const char *filename, uint32_t disksize)
{
    // TODO: iterate through supported images matching ext and filesize

    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        const char *ext = filename + l - 3;

        auto it = std::find_if(
                supported_images.begin(),
                supported_images.end(),
                [ext, disksize](const DiskImageDetails& img) {
                    return (strcasecmp(ext, img.file_extension.c_str()) == 0)
                            && (img.media_size == disksize);
                }
        );

        if (it != supported_images.end()) {
            return (*it).media_type;
        }
    }

    return MEDIATYPE_UNKNOWN;
}

uint16_t MediaType::sector_size(uint16_t sector)
{
    (void)sector; // variable sector lengths are evil!

    return DISK_BYTES_PER_SECTOR_SINGLE;
}

uint32_t MediaType::num_sectors() {
    return _media_num_sectors;
}

#endif // NEW_TARGET