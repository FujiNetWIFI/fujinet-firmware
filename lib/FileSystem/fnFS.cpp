#ifdef ESP_PLATFORM
#include <esp_vfs.h>
#endif

#include <sys/stat.h>
#include <cstdio>
#include <cstring>

#include "fnFS.h"
#include "compat_string.h"
#include "../../include/debug.h"


char * FileSystem::_make_fullpath(const char *path)
{
    if(path != nullptr)
    {
        // Build full path
        char *fullpath = (char *)malloc(MAX_PATHLEN);
        strlcpy(fullpath, _basepath, MAX_PATHLEN);
        // Make sure there's a '/' separating the base from the given path
        if(path[0] != '/')
        {
            int l = strlen(fullpath);
            fullpath[l] = '/';
            fullpath[l+1] = '\0';
        }
        strlcat(fullpath, path, MAX_PATHLEN);

        return fullpath;
    }
    return nullptr;
}


// Returns size of open file
file_size_t FileSystem::filesize(FILE *f)
{
    if (f == nullptr)
        return file_size_t(-1);
    long curr = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, curr, SEEK_SET);
    return file_size_t(end);
}


#ifndef FNIO_IS_STDIO
file_size_t FileSystem::filesize(FileHandler *fh)
{
    long curr = fh->tell();
    fh->seek(0, SEEK_END);
    long end = fh->tell();
    fh->seek(curr, SEEK_SET);
    Debug_printf("FileSystem::filesize from FileHandler returned %ld\r\n", end);
    return file_size_t(end);
}
#endif

// TODO: implement per filesystem
// Returns size of file given path
file_size_t FileSystem::filesize(const char *path)
{
#ifndef ESP_PLATFORM
    Debug_println("!!! TODO !!! FileSystem::filesize from path");
#endif
    struct stat fstat;
    if( 0 == stat(path, &fstat))
        return file_size_t(fstat.st_size);
    return file_size_t(-1);
}

const char * FileSystem::type_to_string(fsType type)
{
    switch(type)
    {
        case FSTYPE_SPIFFS:
            return "FS_SPIFFS";
        case FSTYPE_LITTLEFS:
            return "FS_LITTLEFS";
        case FSTYPE_SDFAT:
            return "FS_SDFAT";
        case FSTYPE_TNFS:
            return "FS_TNFS";
        case FSTYPE_SMB:
            return "FS_SMB";
        case FSTYPE_NFS:
            return "FS_NFS";
        case FSTYPE_FTP:
            return "FS_FTP";
        case FSTYPE_HTTP:
            return "FS_HTTP";
        default:
            return "UNKNOWN FS TYPE";
    }
}

success_is_true FileSystem::dir_open(const char *path, const char *pattern, uint16_t diroptions)
{
    Debug_printf("FileSystem::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diroptions);

    if (ready_to_list().is_error() || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    if (!_dircache)
    {
#ifdef ESP_PLATFORM
        PSRAMAllocator<DirCache> allocator;
        void *memory = allocator.allocate(1);
        if (memory == nullptr)
            RETURN_ERROR_AS_FALSE();
        _dircache.reset(new (memory) DirCache());
#else
        _dircache.reset(new DirCache());
#endif
    }

    if (keeps_listing() && _dircache->holds(path))
    {
        Debug_printf("Use directory cache\n");
    }
    else
    {
        Debug_printf("Fill directory cache\n");

        _dircache->clear();

        if (fill_dircache(path).is_error())
        {
            _dircache->clear();
            RETURN_ERROR_AS_FALSE();
        }

        _dircache->set_path(path);
    }

    // Apply pattern matching filter and sort entries
    return _dircache->apply_filter(pattern, diroptions);
}

void FileSystem::dir_close()
{
    if (_dircache && !keeps_listing())
        _dircache->clear();
}
