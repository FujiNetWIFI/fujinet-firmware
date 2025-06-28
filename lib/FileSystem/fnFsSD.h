#ifndef _FN_FSSD_
#define _FN_FSSD_

#ifdef ESP_PLATFORM
#include "esp_vfs_fat.h"
#endif

#include <stdio.h>

#include "fnFS.h"

class FileSystemSDFAT : public FileSystem
{
private:
#ifdef ESP_PLATFORM
    FF_DIR _dir;
#else
    DIR * _dir;
#endif
    uint64_t _card_capacity = 0;
public:
#ifdef ESP_PLATFORM
    bool start();
#else
    bool start(const char *sd_path = nullptr);
#endif
    virtual bool is_global() override { return true; };

    fsType type() override { return FSTYPE_SDFAT; };
    const char * typestring() override { return type_to_string(FSTYPE_SDFAT); };

    long filesize(const char *filepath) override;

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool is_dir(const char *path) override;
    bool mkdir(const char* path) override;
    bool rmdir(const char* path) override;
    bool dir_exists(const char* path) override { return true; };

    bool dir_open(const char * path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t) override;

    bool create_path(const char *path);
    
    uint64_t card_size();
    uint64_t total_bytes();
    uint64_t used_bytes();
    const char *partition_type();

    // TODO: make it part of base FileSystem class (similar to filesize)
    long mtime(const char *path);
};

extern FileSystemSDFAT fnSDFAT;

#endif // _FN_FSSD_
