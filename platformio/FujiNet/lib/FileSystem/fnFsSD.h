#ifndef _FN_FSSD_
#define _FN_FSSD_

#include "esp_vfs_fat.h"
#include "fnFS.h"
class FileSystemSDFAT : public FileSystem
{
private:
    FF_DIR _dir;
    uint64_t _card_capacity = 0;
public:
    bool start();

    fsType type() override { return FSTYPE_SDFAT; };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool dir_open(const char * path) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;

    uint64_t card_size();
    uint64_t total_bytes();
    uint64_t used_bytes();
    const char *partition_type();
};

extern FileSystemSDFAT fnSDFAT;

#endif // _FN_FSSD_
