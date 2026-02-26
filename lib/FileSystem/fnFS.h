#ifndef _FN_FS_
#define _FN_FS_

#include <time.h>

#include <stdio.h>
#include <cstdint>

#include "fnio.h"

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

#ifdef ESP_PLATFORM
#define MAX_PATHLEN 256
#else
#define MAX_PATHLEN 1024
#endif

#define FNFS_INVALID_DIRPOS 0xFFFF

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

#define DIR_OPTION_DESCENDING 0x0001 // Sort descending, not ascending
#define DIR_OPTION_FILEDATE 0x0002 // Sort by date, not name

struct fsdir_entry
{
    char filename[MAX_PATHLEN];
    bool isDir;
    uint32_t size;
    time_t modified_time;
};
typedef struct fsdir_entry fsdir_entry_t;

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

    char *_make_fullpath(const char *path);

public:
    virtual ~FileSystem() {};

    // The global (fnSDFAT and fsFlash) will return true so we can check before attempting to free/delete
    virtual bool is_global() { return false; };

    virtual bool running() { return _started; };
    virtual const char * basepath() { return _basepath; };
    
    virtual fsType type()=0;
    virtual const char *typestring()=0;

    static const char *type_to_string(fsType type);

    static long filesize(FILE *);
#ifndef FNIO_IS_STDIO
    static long filesize(FileHandler *);
#endif
    virtual long filesize(const char *path);

    // Different FS implemenations may require different startup parameters,
    // so each should define its own version of start()
    //virtual bool start()=0;

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

    virtual bool remove(const char* path) = 0;

    virtual bool rename(const char* pathFrom, const char* pathTo) = 0;

    virtual bool is_dir(const char *path) = 0;
    virtual bool mkdir(const char* path) = 0;
    virtual bool rmdir(const char* path) = 0;
    virtual bool dir_exists(const char* path) = 0;

    // By default, a directory should be sorted and special/hidden items should be filtered out
    virtual bool dir_open(const char *path, const char *pattern, uint16_t diroptions) = 0;
    virtual fsdir_entry_t *dir_read() = 0;
    virtual void dir_close() = 0;
    // Returns current position in directory stream or FNFS_INVALID_DIRPOS on error
    virtual uint16_t dir_tell() = 0;
    // Sets current position in directory stream. Returns false on error.
    virtual bool dir_seek(uint16_t position) = 0;
};

#endif //_FN_FS_
