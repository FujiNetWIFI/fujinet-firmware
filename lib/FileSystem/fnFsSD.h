#ifndef _FN_FSSD_
#define _FN_FSSD_

#include <esp_vfs_fat.h>

#include <stdio.h>

#include "fnFS.h"

class FileSystemSDFAT : public FileSystem
{
private:
    FF_DIR _dir;
    uint64_t _card_capacity = 0;
public:
    bool start();
    virtual bool is_global() override { return true; };

    fsType type() override { return FSTYPE_SDFAT; };
    const char * typestring() override { return type_to_string(FSTYPE_SDFAT); };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool is_dir(const char *path);
    bool dir_open(const char * path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t) override;

    bool create_path(const char *fullpath);
    
    uint64_t card_size();
    uint64_t total_bytes();
    uint64_t used_bytes();
    const char *partition_type();
};

extern FileSystemSDFAT fnSDFAT;

#endif // _FN_FSSD_
