#ifndef _FN_FSSPIF_
#define _FN_FSSPIF_

#include <dirent.h>
#include "fnFS.h"
class SpifFileSystem : public FileSystem
{
private:
    DIR * _dir;
public:
    bool start();
    
    fsType type() override { return FSTYPE_SPIFFS; };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool dir_open(const char * path) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;

    uint64_t total_bytes();
    uint64_t used_bytes();
};

extern SpifFileSystem fnSPIFFS;

#endif // _FN_FSSPIF_
