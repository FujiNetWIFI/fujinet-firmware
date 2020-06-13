#ifndef _FN_FS_
#define _FN_FS_

#include <dirent.h>
#include "../../include/debug.h"

#ifndef FILE_READ
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
#endif

#define MAX_PATHLEN 256

enum fsType
{
    FSTYPE_SPIFFS = 0,
    FSTYPE_SDFAT,
    FSTYPE_TNFS,
    FSTYPE_COUNT
};


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
    char _basepath[20] = { '\0' };
    bool _started = false;
    fsdir_entry _direntry;

    char *_make_fullpath(const char *path);

public:
    virtual ~FileSystem() {};

    // The global (fnSDFAT and fnSPIFFS) will return true so we can check before attempting to free/delete
    virtual bool is_global() { return false; };

    virtual bool running() { return _started; };
    virtual const char * basepath() { return _basepath; };
    
    virtual fsType type()=0;
    virtual const char *typestring()=0;

    static const char *type_to_string(fsType type);

    static long filesize(FILE *);
    static long filesize(const char *filepath);

    // Different FS implemenations may require different startup parameters,
    // so each should define its own version of start()
    //virtual bool start()=0;

    virtual FILE * file_open(const char* path, const char* mode = FILE_READ) = 0;

    virtual bool exists(const char* path) = 0;

    virtual bool remove(const char* path) = 0;

    virtual bool rename(const char* pathFrom, const char* pathTo) = 0;

    virtual bool dir_open(const char *path) = 0;
    virtual fsdir_entry_t *dir_read() = 0;
    virtual void dir_close() = 0;
};

#endif //_FN_FS_
