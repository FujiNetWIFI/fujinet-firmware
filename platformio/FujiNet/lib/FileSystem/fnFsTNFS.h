#ifndef _FN_FSTNFS_
#define _FN_FSTNFS_

#include "fnFS.h"
#include "../TNFSlib/tnfslib.h"


class TnfsFileSystem : public FileSystem
{
private:
    bool _connected = false;
    tnfsMountInfo _mountinfo;
    unsigned long _last_dns_refresh;

public:
    TnfsFileSystem();
    ~TnfsFileSystem();

    bool start(const char *host, uint16_t port=TNFS_DEFAULT_PORT, const char * mountpath=nullptr, const char * userid=nullptr, const char * password=nullptr);

    fsType type() override { return FSTYPE_TNFS; };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override {return NULL;};

    bool exists(const char* path) override {return false;};

    bool remove(const char* path) override {return false;};

    bool rename(const char* pathFrom, const char* pathTo) override {return false;};

    bool dir_open(const char * path) override;
    fsdir_entry *dir_read() override;
    void dir_close();
};

#endif // _FN_FSTNFS_