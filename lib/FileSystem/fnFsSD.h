#ifndef _FN_FSSD_
#define _FN_FSSD_

#ifdef ESP_PLATFORM
#include "esp_vfs_fat.h"
#endif

#include <stdio.h>
#include <vector>

#include "fnFS.h"
#include "fnFsDirCache.h"

class FileSystemSDFAT : public FileSystemDirCache
{
private:
#ifdef ESP_PLATFORM
    FF_DIR _dir;
#else
    DIR * _dir;
#endif
    uint64_t _card_capacity = 0;
public:
    FileSystemSDFAT() = default;
    // Another handle on the mounted card, with its own directory listing
    FileSystemSDFAT(const FileSystemSDFAT &card)
        : FileSystemDirCache(card), _card_capacity(card._card_capacity) {}

#ifdef ESP_PLATFORM
    success_is_true start();
#else
    success_is_true start(const char *sd_path = nullptr);
#endif
    virtual bool is_global() override { return true; };

    fsType type() override { return FSTYPE_SDFAT; };
    const char * typestring() override { return type_to_string(FSTYPE_SDFAT); };

    file_size_t filesize(const char *filepath) override;

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool exists(const char* path) override;

    success_is_true remove(const char* path) override;

    success_is_true rename(const char* pathFrom, const char* pathTo) override;

    bool is_dir(const char *path) override;
    success_is_true mkdir(const char* path) override;
    success_is_true rmdir(const char* path) override;
    bool dir_exists(const char* path) override { return true; };

    success_is_true create_path(const char *path);
    
    uint64_t card_size();
    uint64_t total_bytes();
    uint64_t used_bytes();
    const char *partition_type();

    // TODO: make it part of base FileSystem class (similar to filesize)
    long mtime(const char *path);
protected:
    success_is_true fill_dircache(const char *path) override;
    success_is_true ready_to_list() override { RETURN_SUCCESS_AS_TRUE(); }
    // The card changes under it, so each open reads it afresh
    bool keeps_listing() override { return false; }
};

extern FileSystemSDFAT fnSDFAT;

#endif // _FN_FSSD_
