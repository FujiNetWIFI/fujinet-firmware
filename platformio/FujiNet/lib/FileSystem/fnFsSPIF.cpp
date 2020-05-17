#include <esp_vfs.h>
#include "esp_spiffs.h"

#include "fnFsSPIF.h"
#include "../../include/debug.h"

#define SPIFFS_MAXPATH 512

// Our global SD interface
SpifFileSystem fnSPIFFS;

bool SpifFileSystem::dir_open(const char * path)
{
    char * fpath = _make_fullpath(path);
    _dir = opendir(fpath);
    delete fpath;
    return(_dir != nullptr);
}
struct dirent * SpifFileSystem::dir_read()
{
    return readdir(_dir);
}
void SpifFileSystem::dir_close()
{
    closedir(_dir);
}

FILE * SpifFileSystem::file_open(const char* path, const char* mode)
{
    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    delete fpath;
    return result;
}

bool SpifFileSystem::exists(const char* path)
{
    char * fpath = _make_fullpath(path);
    struct stat st;
    int i = stat(fpath, &st);
#ifdef DEBUG
    //Debug_printf("SpifFileSystem::exists returned %d on \"%s\" (%s)\n", i, path, fpath);
#endif
    delete fpath;
    return (i == 0);
}

bool SpifFileSystem::remove(const char* path)
{
    char * fpath = _make_fullpath(path);
    int i = ::remove(fpath);
#ifdef DEBUG
    Debug_printf("SpifFileSystem::remove returned %d on \"%s\" (%s)\n", i, path, fpath);
#endif
    delete fpath;
    return (i == 0);
}

bool SpifFileSystem::rename(const char* pathFrom, const char* pathTo)
{
    char * spath = _make_fullpath(pathFrom);
    char * dpath = _make_fullpath(pathTo);
    int i = ::rename(spath, dpath);
#ifdef DEBUG
    Debug_printf("SpifFileSystem::rename returned %d on \"%s\" -> \"%s\" (%s -> %s)\n", i, pathFrom, pathTo, spath, dpath);
#endif
    delete spath;
    delete dpath;
    return (i == 0);
}

uint64_t SpifFileSystem::total_bytes()
{
    size_t total = 0, used = 0;
	esp_spiffs_info(NULL, &total, &used);
    return (uint64_t)total;
}

uint64_t SpifFileSystem::used_bytes()
{
    size_t total = 0, used = 0;
	esp_spiffs_info(NULL, &total, &used);
    return (uint64_t)used;
}

bool SpifFileSystem::start()
{
    if(_started)
        return true;
        
    static const char * bp = "/spiffs";
    _basepath = bp;

    esp_vfs_spiffs_conf_t conf = {
      .base_path = _basepath,
      .partition_label = NULL,
      .max_files = 10, // from SPIFFS.h
      .format_if_mount_failed = false
    };
    
    esp_err_t e = esp_vfs_spiffs_register(&conf);

    if (e != ESP_OK)
    {
        #ifdef DEBUG
        Debug_printf("Failed to mount SPIFFS partition, err = %d\n", e);
        #endif
        _started = false;
    }
    else
    {
        _started = true;
    #ifdef DEBUG        
        Debug_println("SPIFFS mounted.");
        /*
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        Debug_printf("  partition size: %u, used: %u, free: %u\n", total, used, total-used);
        */
    #endif
    }

    return _started;
}
