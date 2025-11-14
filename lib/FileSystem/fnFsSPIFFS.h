#ifndef _FN_FSSPIFFS_
#define _FN_FSSPIFFS_
#ifdef FLASH_SPIFFS

#include "compat_dirent.h"

#include "fnFS.h"


class FileSystemSPIFFS : public FileSystem
{
private:
    DIR * _dir = nullptr;
public:
    FileSystemSPIFFS();
    bool start();
    bool stop();
    
    fsType type() override { return FSTYPE_SPIFFS; };
    const char * typestring() override { return type_to_string(FSTYPE_SPIFFS); };

    virtual bool is_global() override { return true; };    

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool is_dir(const char *path) override;
    bool mkdir(const char* path) override { return true; };
    bool rmdir(const char* path) override { return true; };
    bool dir_exists(const char* path) override { return true; };

    bool dir_open(const char * path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t) override;

    uint64_t total_bytes();
    uint64_t used_bytes();
};

extern FileSystemSPIFFS fsFlash;

#endif // FLASH_SPIFFS
#endif // _FN_FSSPIFFS_
