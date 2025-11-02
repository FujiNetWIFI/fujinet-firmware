#ifndef _FN_FSLITTLEFS_
#define _FN_FSLITTLEFS_
#ifdef FLASH_LITTLEFS

#include <dirent.h>

#include "fnFS.h"


class FileSystemLittleFS : public FileSystem
{
private:
    DIR * _dir = nullptr;
public:
    FileSystemLittleFS();
    bool start();
    bool stop();
    
    fsType type() override { return FSTYPE_LITTLEFS; };
    const char * typestring() override { return type_to_string(FSTYPE_LITTLEFS); };

    virtual bool is_global() override { return true; };    

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool is_dir(const char *path);
    bool mkdir(const char* path) override { return true; };
    bool rmdir(const char* path) override { return true; };
    bool dir_exists(const char* path) override { return true; };

    bool dir_open(const char * path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t) override;

    bool create_path(const char *fullpath);

    uint64_t total_bytes();
    uint64_t used_bytes();
};

extern FileSystemLittleFS fsFlash;

#endif // FLASH_LITTLEFS
#endif // _FN_FSLITTLEFS_
