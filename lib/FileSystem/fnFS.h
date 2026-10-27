#ifndef _FN_FS_
#define _FN_FS_

#include <time.h>

#include <stdio.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "fnio.h"
#include "global_types.h"
#include "fnFSDirEntry.h"
#include "fnDirCache.h"

#ifndef ESP_PLATFORM
#include "compat_dirent.h"
#endif

#ifndef FILE_READ
#define FILE_READ "rb"
#define FILE_WRITE "wb"
#define FILE_APPEND "ab"
#define FILE_READ_WRITE "r+b"
#define FILE_READ_TEXT "rt"
#define FILE_WRITE_TEXT "wt"
#define FILE_APPEND_TEXT "at"
#define FILE_READ_WRITE_TEXT "r+t"
#endif

enum fsType
{
    FSTYPE_SPIFFS = 0,
    FSTYPE_LITTLEFS,
    FSTYPE_SDFAT,
    FSTYPE_TNFS,
    FSTYPE_SMB,
    FSTYPE_NFS,
    FSTYPE_FTP,
    FSTYPE_HTTP,
    FSTYPE_COUNT
};


class FileSystem
{
protected:
#ifdef ESP_PLATFORM
    char _basepath[20] = { '\0' };
#else
    char _basepath[MAX_PATHLEN] = { '\0' };
#endif
    bool _started = false;
    fsdir_entry _direntry;

    // The listing dir_open() reads with fill_dircache(), made on first use and held in PSRAM so
    // an idle filesystem costs one pointer of DRAM. A filesystem that pages its own listing
    // overrides the dir_* functions and never allocates it.
#ifdef ESP_PLATFORM
    std::unique_ptr<DirCache, PSRAMDeleter<DirCache>> _dircache;
#else
    std::unique_ptr<DirCache> _dircache;
#endif

    char *_make_fullpath(const char *path);

    // Add every entry of path to _dircache
    virtual success_is_true fill_dircache(const char *path) { RETURN_ERROR_AS_FALSE(); }

    // Whether the filesystem can list right now
    virtual success_is_true ready_to_list() { RETURN_SUCCESS_IF(_started); }

    // Whether the listing outlives dir_close(), so opening the same path again skips fill_dircache()
    virtual bool keeps_listing() { return true; }

public:
    FileSystem() = default;
    // Another handle on the same filesystem, with a listing of its own
    FileSystem(const FileSystem &other) : _started(other._started), _direntry(other._direntry)
    {
        memcpy(_basepath, other._basepath, sizeof(_basepath));
    }
    virtual ~FileSystem() {};

    // The globals (fnSDFAT and fsFlash) will return true so we can check before attempting to free/delete
    virtual bool is_global() { return false; };

    virtual bool running() { return _started; };
    virtual const char * basepath() { return _basepath; };
    
    virtual fsType type()=0;
    virtual const char *typestring()=0;

    static const char *type_to_string(fsType type);

    static file_size_t filesize(FILE *);
#ifndef FNIO_IS_STDIO
    static file_size_t filesize(FileHandler *);
#endif
    virtual file_size_t filesize(const char *path);

    virtual FILE * file_open(const char* path, const char* mode = FILE_READ) = 0;
#ifdef FNIO_IS_STDIO
    fnFile * fnfile_open(const char* path, const char* mode = FILE_READ) {
        return file_open(path, mode);
    }
#else
    virtual FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) = 0;
    fnFile * fnfile_open(const char* path, const char* mode = FILE_READ) {
        return filehandler_open(path, mode);
    }
#endif

    virtual bool exists(const char* path) = 0;

    virtual success_is_true remove(const char* path) = 0;

    virtual success_is_true rename(const char* pathFrom, const char* pathTo) = 0;

    virtual bool is_dir(const char *path) = 0;
    virtual success_is_true mkdir(const char* path) = 0;
    virtual success_is_true rmdir(const char* path) = 0;
    virtual bool dir_exists(const char* path) = 0;

    // By default, a directory should be sorted and special/hidden items should be filtered out
    virtual success_is_true dir_open(const char *path, const char *pattern, uint16_t diroptions);
    virtual fsdir_entry_t *dir_read() { return _dircache ? _dircache->read(_direntry) : nullptr; }
    virtual void dir_close();
    // Returns current position in directory stream or FNFS_INVALID_DIRPOS on error
    virtual uint16_t dir_tell() { return _dircache ? _dircache->tell() : FNFS_INVALID_DIRPOS; }
    // Sets current position in directory stream.
    virtual success_is_true dir_seek(uint16_t position)
    {
        RETURN_SUCCESS_IF(_dircache && _dircache->seek(position).is_success());
    }
};

#endif //_FN_FS_
