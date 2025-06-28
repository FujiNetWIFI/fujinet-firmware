#ifdef FLASH_LITTLEFS

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include "fnFsLittleFS.h"
#include "fnFileLocal.h"

#include <esp_vfs.h>
#include <errno.h>
#include <filesystem>

#include "esp_littlefs.h"

#include "../../include/debug.h"

#define LITTLEFS_MAXPATH 512

// Our global LITTLEFS interface
FileSystemLittleFS fsFlash;

FileSystemLittleFS::FileSystemLittleFS()
{
    // memset(_dir,0,sizeof(DIR));
}

bool FileSystemLittleFS::is_dir(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat info;
    stat( fpath, &info);
    return (info.st_mode == S_IFDIR) ? true: false;
}

bool FileSystemLittleFS::dir_open(const char * path, const char * pattern, uint16_t diropts)
{
    // We ignore sorting options since we don't expect user browsing on LITTLEFS
    char * fpath = _make_fullpath(path);
    _dir = opendir(fpath);
    free(fpath);
    return(_dir != nullptr);
}

fsdir_entry * FileSystemLittleFS::dir_read()
{
    if(_dir == nullptr)
        return nullptr;

    struct dirent *d;
    d = readdir(_dir);
    if(d != nullptr)
    {
        strlcpy(_direntry.filename, d->d_name, sizeof(_direntry.filename));

        _direntry.isDir = (d->d_type & DT_DIR) ? true : false;

        _direntry.size = 0;
        _direntry.modified_time = 0;

        // timestamps aren't stored when files are uploaded during firmware deployment
        char * fpath = _make_fullpath(_direntry.filename);
        struct stat s;
        if(stat(fpath, &s) == 0)
        {
            _direntry.size = s.st_size;
            _direntry.modified_time = s.st_mtime;
        }
        #ifdef DEBUG
            // Debug_printv("stat \"%s\" errno %d\r\n", fpath, errno);
        #endif
        return &_direntry;
    }
    return nullptr;
}

void FileSystemLittleFS::dir_close()
{
    closedir(_dir);
    _dir = nullptr;
}

uint16_t FileSystemLittleFS::dir_tell()
{
    return 0;
}

bool FileSystemLittleFS::dir_seek(uint16_t)
{
    return false;
}


/* Checks that path exists and creates if it doesn't including any parent directories
   Each directory along the path is limited to 64 characters
   An initial "/" is optional, but you should not include an ending "/"

   Examples:
   "abc"
   "/abc"
   "/abc/def"
   "abc/def/ghi"
*/
bool FileSystemLittleFS::create_path(const char *fullpath)
{
    char segment[64];

    const char *end = fullpath;
    bool done = false;

    while (!done)
    {
        bool found = false;

        if(*end == '\0')
        {
            done = true;
            // Only indicate we found a segment if we're not still pointing to the start
            if(end != fullpath)
                found = true;
        } else if(*end == '/')
        {
            // Only indicate we found a segment if this isn't a starting '/'
            if(end != fullpath)
                found = true;
        }

        if(found)
        {
            /* We copy the segment from the fullpath using a length of (end - fullpath) + 1
               This allows for the ending terminator but not for the trailing '/'
               If we're done (at the end of fullpath), we assume there's no  trailing '/' so the length
               is (end - fullpath) + 2
            */
            strlcpy(segment, fullpath, end - fullpath + (done ? 2 : 1));
            //Debug_printf("Checking/creating directory: \"%s\"\r\n", segment);
            if ( !exists(segment) )
            {
                if( !std::filesystem::create_directory(segment) )
                {
                    Debug_printf("FAILED errno=%d\r\n", errno);
                }
            }
        }

        end++;
    }

    return true;
}

FILE * FileSystemLittleFS::file_open(const char* path, const char* mode)
{
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    return result;
}

#ifndef FNIO_IS_STDIO
FileHandler * FileSystemLittleFS::filehandler_open(const char* path, const char* mode)
{
    Debug_printf("FileSystemLittleFS::filehandler_open %s %s\n", path, mode);
    FILE * fh = file_open(path, mode);
    return (fh == nullptr) ? nullptr : new FileHandlerLocal(fh);
}
#endif

bool FileSystemLittleFS::exists(const char* path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
#ifdef DEBUG
    //Debug_printv("FileSystemLittleFS::exists returned %d on \"%s\" (%s)\r\n", i, path, fpath);
#endif
    free(fpath);
    return (i == 0);
}

bool FileSystemLittleFS::remove(const char* path)
{
    char * fpath = _make_fullpath(path);
    int i = ::remove(fpath);
#ifdef DEBUG
    Debug_printv("FileSystemLittleFS::remove returned %d on \"%s\" (%s)\r\n", i, path, fpath);
#endif
    free(fpath);
    return (i == 0);
}

bool FileSystemLittleFS::rename(const char* pathFrom, const char* pathTo)
{
    char * spath = _make_fullpath(pathFrom);
    char * dpath = _make_fullpath(pathTo);
    int i = ::rename(spath, dpath);
#ifdef DEBUG
    Debug_printv("FileSystemLittleFS::rename returned %d on \"%s\" -> \"%s\" (%s -> %s)\r\n", i, pathFrom, pathTo, spath, dpath);
#endif
    free(spath);
    free(dpath);
    return (i == 0);
}

uint64_t FileSystemLittleFS::total_bytes()
{
    size_t total = 0, used = 0;
	esp_littlefs_info(NULL, &total, &used);
    return (uint64_t)total;
}

uint64_t FileSystemLittleFS::used_bytes()
{
    size_t total = 0, used = 0;
	esp_littlefs_info(NULL, &total, &used);
    return (uint64_t)used;
}

bool FileSystemLittleFS::start()
{
    if(_started)
        return true;

    // Set our basepath
    // strlcpy(_basepath, "/flash", sizeof(_basepath));

    esp_vfs_littlefs_conf_t conf = {
      .base_path = "",
      .partition_label = "storage",
      .format_if_mount_failed = false,
      .dont_mount = false
    };
    
    esp_err_t e = esp_vfs_littlefs_register(&conf);

    if (e != ESP_OK)
    {
        #ifdef DEBUG
        Debug_printv("Failed to mount LittleFS partition, err = %d\r\n", e);
        #endif
        _started = false;
    }
    else
    {
        _started = true;
    #ifdef DEBUG        
        Debug_println("LittleFS mounted.");
        /*
        size_t total = 0, used = 0;
        esp_littlefs_info(NULL, &total, &used);
        Debug_printv("  partition size: %u, used: %u, free: %u\r\n", total, used, total-used);
        */
    #endif
    }

    return _started;
}

#endif // FLASH_LITTLEFS
