#ifdef BUILD_RS232 // temporary

#include "diskTypeROM.h"

#include <cstring>

#include "../../include/debug.h"
#include "../../fuji/fujiHost.h"

#include "compat_string.h"

// _disk_filename arrives already host-prefixed (fnfile_open rewrites
// disk.filename in place); fujiHost APIs prefix again, so strip it first.
static const char *strip_host_prefix(fujiHost *host, const char *filename)
{
    const char *pfx = host->get_prefix();
    if (pfx == nullptr || pfx[0] == '\0')
        return filename;

    size_t plen = strlen(pfx);
    if (strncmp(filename, pfx, plen) != 0)
        return filename; // not prefixed after all

    const char *p = filename + plen;
    // skip the separator util_concat_paths() inserted
    if (pfx[plen - 1] != '/' && pfx[plen - 1] != '\\' && (*p == '/' || *p == '\\'))
        p++;
    return p;
}

error_is_true MediaTypeROM::read(uint32_t sectornum, uint32_t *readcount)
{
    Debug_print("ROM READ not supported\r\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::write(uint32_t sectornum, bool verify)
{
    Debug_print("ROM WRITE not supported\r\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::format(uint32_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

void MediaTypeROM::status(uint8_t statusbuff[4])
{
    memset(statusbuff, 0, 4);
}

void MediaTypeROM::resolve_cfg()
{
    char path[sizeof(_cfg_path)];
    strlcpy(path, strip_host_prefix(_media_host, _disk_filename), sizeof(path));

    // replace the basename's extension only
    char *base = path;
    for (char *p = path; *p != '\0'; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    char *dot = strrchr(base, '.');
    if (dot != nullptr)
        strlcpy(dot, ".cfg", sizeof(path) - (dot - path));
    else
        strlcat(path, ".cfg", sizeof(path));

    if (!_media_host->file_exists(path))
    {
        // case-sensitive hosts may carry the sibling as .CFG
        size_t len = strlen(path);
        memcpy(path + len - 4, ".CFG", 4);
        if (!_media_host->file_exists(path))
        {
            // Not an error -- a .bin with no memory map boots against the
            // emulator's size-guess table -- but for the titles that need one
            // the result is a wrong map, i.e. a game that boots to garbage
            // with nothing anywhere saying why. Say it here.
            Debug_printv("MediaTypeROM: no .cfg sibling for %s (tried \"%s\" in both "
                         "casings) -- booting with a default memory map\n",
                         _disk_filename, path);
            return;
        }
    }

    strlcpy(_cfg_path, path, sizeof(_cfg_path));
}

fnFile *MediaTypeROM::cfg_open(uint32_t *size)
{
    *size = 0;
    if (_cfg_path[0] == '\0')
        return nullptr;

    char resolved[sizeof(_cfg_path)];
    strlcpy(resolved, _cfg_path, sizeof(resolved));

    fnFile *f = _media_host->fnfile_open(_cfg_path, resolved, sizeof(resolved), "rb");
    if (f == nullptr)
    {
        Debug_printv("MediaTypeROM: .cfg sibling exists but failed to open: %s\n", _cfg_path);
        return nullptr;
    }

    long cfgsize = _media_host->file_size(f);
    if (cfgsize < 0)
    {
        Debug_printv("MediaTypeROM: .cfg sibling has no usable size: %s\n", _cfg_path);
        fnio::fclose(f);
        return nullptr;
    }

    *size = (uint32_t)cfgsize;
    return f;
}

void MediaTypeROM::cfg_close(fnFile *f)
{
    if (f != nullptr)
        fnio::fclose(f);
}

mediatype_t MediaTypeROM::mount(fnFile *f, uint32_t disksize)
{
    Debug_printv("MediaTypeROM MOUNT %s (%lu bytes)\n",
                 _disk_filename[0] != '\0' ? _disk_filename : "?", (unsigned long)disksize);

    _disk_fileh = f;
    _disk_image_size = disksize;
    _disktype = MEDIATYPE_ROM;
    _cfg_path[0] = '\0';

    if (_media_host != nullptr && _disk_filename[0] != '\0')
        resolve_cfg();

    return _disktype;
}

#endif // BUILD_RS232
