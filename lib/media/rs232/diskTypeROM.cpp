#ifdef BUILD_RS232 // temporary

#include "diskTypeROM.h"

#include <cstring>

#include "../../include/debug.h"
#include "../../fuji/fujiHost.h"

#include "compat_string.h"

// _disk_filename is already host-prefixed; fujiHost APIs prefix again.
static const char *strip_host_prefix(fujiHost *host, const char *filename)
{
    const char *prefix = host->get_prefix();
    if (prefix == nullptr || prefix[0] == '\0')
        return filename;

    size_t prefix_len = strlen(prefix);
    if (strncmp(filename, prefix, prefix_len) != 0)
        return filename; // not prefixed after all

    const char *stripped = filename + prefix_len;
    // skip the separator util_concat_paths() inserted
    if (prefix[prefix_len - 1] != '/' && prefix[prefix_len - 1] != '\\' &&
        (*stripped == '/' || *stripped == '\\'))
        stripped++;
    return stripped;
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

void MediaTypeROM::resolve_memory_map()
{
    char path[sizeof(_map_path)];
    strlcpy(path, strip_host_prefix(_media_host, _disk_filename), sizeof(path));

    // replace the basename's extension only
    char *base = path;
    for (char *cursor = path; *cursor != '\0'; cursor++)
        if (*cursor == '/' || *cursor == '\\')
            base = cursor + 1;
    char *dot = strrchr(base, '.');
    if (dot != nullptr)
        strlcpy(dot, ".cfg", sizeof(path) - (dot - path));
    else
        strlcat(path, ".cfg", sizeof(path));

    if (!_media_host->file_exists(path))
    {
        // case-sensitive hosts may carry the sibling as .CFG
        size_t path_len = strlen(path);
        memcpy(path + path_len - 4, ".CFG", 4);
        if (!_media_host->file_exists(path))
        {
            // Not an error, but a wrong map boots to garbage with no clue why.
            Debug_printv("MediaTypeROM: no .cfg sibling for %s (tried \"%s\" in both "
                         "casings) -- booting with a default memory map\n",
                         _disk_filename, path);
            return;
        }
    }

    strlcpy(_map_path, path, sizeof(_map_path));
}

bool MediaTypeROM::open_memory_map()
{
    if (_map_fileh != nullptr)
        return true;
    if (_map_path[0] == '\0')
        return false;

    char resolved[sizeof(_map_path)];
    strlcpy(resolved, _map_path, sizeof(resolved));

    _map_fileh = _media_host->fnfile_open(_map_path, resolved, sizeof(resolved), "rb");
    if (_map_fileh == nullptr)
    {
        Debug_printv("MediaTypeROM: memory map exists but failed to open: %s\n", _map_path);
        return false;
    }

    long map_size = _media_host->file_size(_map_fileh);
    if (map_size < 0)
    {
        Debug_printv("MediaTypeROM: memory map has no usable size: %s\n", _map_path);
        close_memory_map();
        return false;
    }

    _map_size = (uint32_t)map_size;
    _map_pos = 0;
    return true;
}

void MediaTypeROM::close_memory_map()
{
    if (_map_fileh != nullptr)
    {
        fnio::fclose(_map_fileh);
        _map_fileh = nullptr;
    }
    _map_pos = 0;
}

uint32_t MediaTypeROM::stream_size(RomStream source)
{
    if (source == RomStream::Image)
        return _disk_image_size;

    return open_memory_map() ? _map_size : 0;
}

size_t MediaTypeROM::stream_read(RomStream source, uint32_t offset, uint8_t *buffer, size_t length)
{
    fnFile *fileh;
    uint32_t *position;
    uint32_t total;

    if (source == RomStream::Image)
    {
        fileh = _disk_fileh;
        position = &_image_pos;
        total = _disk_image_size;
    }
    else
    {
        if (!open_memory_map())
            return 0;
        fileh = _map_fileh;
        position = &_map_pos;
        total = _map_size;
    }

    if (fileh == nullptr || offset >= total)
        return 0;

    if (length > total - offset)
        length = total - offset;

    if (*position != offset)
    {
        if (fnio::fseek(fileh, offset, SEEK_SET) != 0)
            return 0;
        *position = offset;
    }

    size_t got = fnio::fread(buffer, 1, length, fileh);
    *position += got;

    // the map is read once, by the mount that pushes it
    if (source == RomStream::MemoryMap && *position >= total)
        close_memory_map();

    return got;
}

mediatype_t MediaTypeROM::mount(fnFile *fileh, uint32_t disksize)
{
    Debug_printv("MediaTypeROM MOUNT %s (%lu bytes)\n",
                 _disk_filename[0] != '\0' ? _disk_filename : "?", (unsigned long)disksize);

    _disk_fileh = fileh;
    _disk_image_size = disksize;
    _disktype = MEDIATYPE_ROM;
    _map_path[0] = '\0';
    _image_pos = UINT32_MAX;

    if (_media_host != nullptr && _disk_filename[0] != '\0')
        resolve_memory_map();

    return _disktype;
}

void MediaTypeROM::unmount()
{
    close_memory_map();
    MediaType::unmount();
}

MediaTypeROM::~MediaTypeROM()
{
    close_memory_map();
}

#endif // BUILD_RS232
