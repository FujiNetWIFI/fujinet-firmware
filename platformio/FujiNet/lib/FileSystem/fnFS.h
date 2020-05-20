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
    FSTYPE_SPIFFS,
    FSTYPE_SDFAT,
    FSTYPE_TNFS,
    FSTYPE_COUNT
};


class FileSystem
{
protected:
    const char * _basepath;
    bool _started = false;

    char *_make_fullpath(const char *path);

public:
    virtual bool running() { return _started; };
    virtual fsType type()=0;

    static long filesize(FILE *);

    // Different FS implemenations may require different startup parameters,
    // so each should define its own version of start()
    //virtual bool start()=0;

    virtual FILE * file_open(const char* path, const char* mode = FILE_READ) = 0;

    virtual bool exists(const char* path) = 0;

    virtual bool remove(const char* path) = 0;

    virtual bool rename(const char* pathFrom, const char* pathTo) = 0;

    virtual bool dir_open(const char *path) = 0;
    virtual dirent *dir_read() = 0;
    virtual void dir_close() = 0;
};

#endif //_FN_FS_
