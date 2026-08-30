#ifdef BUILD_RS232 // temporary

#include "diskType.h"

#include <string.h>

#include "../../include/debug.h"

#include "utils.h"


#define DENSITY_FM 0
#define DENSITY_MFM 4

#define SIDES_SS 0
#define SIDES_DS 1

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t MediaType::sector_size(uint32_t sectornum)
{
    return 512;
}

// Default WRITE is not implemented
error_is_true MediaType::write(uint32_t sectornum, bool verify)
{
    Debug_print("DISK WRITE NOT IMPLEMENTED\r\n");
    RETURN_ERROR_AS_TRUE();
}

// Default FORMAT is not implemented
error_is_true MediaType::format(uint32_t *responsesize)
{
    Debug_print("DISK FORMAT NOT IMPLEMENTED\r\n");
    RETURN_ERROR_AS_TRUE();
}

// Update PERCOM block from the total # of sectors
void MediaType::derive_percom_block(uint32_t numSectors)
{

}

// Dump PERCOM block
void MediaType::dump_percom_block()
{

}

void MediaType::unmount()
{
    if (_disk_fileh != nullptr)
    {
        fnio::fclose(_disk_fileh);
        _disk_fileh = nullptr;
    }
}

mediatype_t MediaType::discover_mediatype(const char *filename)
{
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "XEX") == 0)
        {
            return MEDIATYPE_IMG;
        }
        // BIN/ROM/INT/ITV are all "load this straight into cart memory and
        // reset" formats on every current BUILD_RS232 platform (Intellivision
        // .bin+.cfg pairs and Intellicart .rom images, Atari 2600 .bin
        // homebrew, CoCo cartridge images) -- there is no disk-image
        // convention on any of them that also uses these extensions, so
        // detecting MEDIATYPE_ROM by extension alone is unambiguous. This
        // replaces the previous disksize==8192||16384||32768 heuristic
        // (rs232Disk::mount()), which misfired on any file of those exact
        // sizes regardless of extension and missed every other ROM size.
        if (strcasecmp(ext, "ROM") == 0 || strcasecmp(ext, "BIN") == 0 ||
            strcasecmp(ext, "INT") == 0 || strcasecmp(ext, "ITV") == 0)
        {
            return MEDIATYPE_ROM;
        }
        // .DSK is a bit-exact raw dump of a floppy disk served by
        // MediaTypeDSK. The host supplies head/track/sector/fmttype on
        // every access; geometry is never inferred from the image.
        if (strcasecmp(ext, "DSK") == 0)
        {
            return MEDIATYPE_DSK;
        }
    }
    return MEDIATYPE_UNKNOWN;
}

MediaType::~MediaType()
{
    unmount();
}

#endif /* BUILD_RS232 */
