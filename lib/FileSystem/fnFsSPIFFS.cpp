#ifdef FLASH_SPIFFS

#include "fnFsSPIFFS.h"

#ifdef ESP_PLATFORM
#include <esp_vfs.h>
#include <esp_spiffs.h>
#else
#include "fnFileLocal.h"
#endif

#include <sys/stat.h>
#include <errno.h>

#include "compat_string.h"
#include "../../include/debug.h"


#define SPIFFS_MAXPATH 512

// Our global SPIFFS interface
FileSystemSPIFFS fsFlash;

FileSystemSPIFFS::FileSystemSPIFFS()
{
    // memset(_dir,0,sizeof(DIR));
}

bool FileSystemSPIFFS::is_dir(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat info;
    stat( fpath, &info);
    return (info.st_mode == S_IFDIR) ? true: false;
}

bool FileSystemSPIFFS::dir_open(const char * path, const char * pattern, uint16_t diropts)
{
    // We ignore sorting options since we don't expect user browsing on SPIFFS
    char * fpath = _make_fullpath(path);
    _dir = opendir(fpath);
    free(fpath);
    return(_dir != nullptr);
}

fsdir_entry * FileSystemSPIFFS::dir_read()
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

        // isDir will always be false - SPIFFS doesn't store directories ("dir/name" is really just "name_part1/name_part2")
        // timestamps aren't stored when files are uploaded during firmware deployment
        char * fpath = _make_fullpath(_direntry.filename);
        struct stat s;
        if(stat(fpath, &s) == 0)
        {
            _direntry.size = s.st_size;
            _direntry.modified_time = s.st_mtime;
        }
        // Debug_printf("stat \"%s\" errno %d\r\n", fpath, errno);
        return &_direntry;
    }
    return nullptr;
}

void FileSystemSPIFFS::dir_close()
{
    closedir(_dir);
    _dir = nullptr;
}

uint16_t FileSystemSPIFFS::dir_tell()
{
    return 0;
}

bool FileSystemSPIFFS::dir_seek(uint16_t)
{
    return false;
}

FILE * FileSystemSPIFFS::file_open(const char* path, const char* mode)
{
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    return result;
}

#ifndef ESP_PLATFORM
FileHandler * FileSystemSPIFFS::filehandler_open(const char* path, const char* mode)
{
    Debug_printf("FileSystemSPIFFS::filehandler_open %s %s\n", path, mode);
    FILE * fh = file_open(path, mode);
    return (fh == nullptr) ? nullptr : new FileHandlerLocal(fh);
}
#endif

bool FileSystemSPIFFS::exists(const char* path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
    //Debug_printf("FileSystemSPIFFS::exists returned %d on \"%s\" (%s)\r\n", i, path, fpath);
    free(fpath);
    return (i == 0);
}

bool FileSystemSPIFFS::remove(const char* path)
{
    char * fpath = _make_fullpath(path);
    int i = ::remove(fpath);
    Debug_printf("FileSystemSPIFFS::remove returned %d on \"%s\" (%s)\r\n", i, path, fpath);
    free(fpath);
    return (i == 0);
}

bool FileSystemSPIFFS::rename(const char* pathFrom, const char* pathTo)
{
    char * spath = _make_fullpath(pathFrom);
    char * dpath = _make_fullpath(pathTo);
    int i = ::rename(spath, dpath);
    Debug_printf("FileSystemSPIFFS::rename returned %d on \"%s\" -> \"%s\" (%s -> %s)\r\n", i, pathFrom, pathTo, spath, dpath);
    free(spath);
    free(dpath);
    return (i == 0);
}

uint64_t FileSystemSPIFFS::total_bytes()
{
    size_t total = 0, used = 0;
#ifdef ESP_PLATFORM
	esp_spiffs_info(NULL, &total, &used);
#endif
    return (uint64_t)total;
}

uint64_t FileSystemSPIFFS::used_bytes()
{
    size_t total = 0, used = 0;
#ifdef ESP_PLATFORM
	esp_spiffs_info(NULL, &total, &used);
#endif
    return (uint64_t)used;
}

bool FileSystemSPIFFS::start()
{
    if(_started)
        return true;

    // Set our basepath
#ifdef ESP_PLATFORM
#ifndef BUILD_IEC
    strlcpy(_basepath, "/spiffs", sizeof(_basepath));
#else
    strlcpy(_basepath, "", sizeof(_basepath));
#endif
// ESP_PLATFORM
#else
// !ESP_PLATFORM
    strlcpy(_basepath, "data", sizeof(_basepath));
#endif


#ifdef ESP_PLATFORM
    esp_vfs_spiffs_conf_t conf = {
      .base_path = _basepath,
      .partition_label = "flash",
      .max_files = 10, // from SPIFFS.h
      .format_if_mount_failed = false
    };
    
    esp_err_t e = esp_vfs_spiffs_register(&conf);

    if (e != ESP_OK)
    {
        Debug_printf("Failed to mount SPIFFS partition, err = %d\r\n", e);
        _started = false;
    }
    else
#endif // ESP_PLATFORM
    {
        _started = true;
        Debug_println("SPIFFS mounted.");
    #ifdef DEBUG
        /*
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        Debug_printf("  partition size: %u, used: %u, free: %u\r\n", total, used, total-used);
        */
    #endif
    }

    return _started;
}

#endif // FLASH_SPIFFS