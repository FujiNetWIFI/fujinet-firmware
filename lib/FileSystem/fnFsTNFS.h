#ifndef _FN_FSTNFS_
#define _FN_FSTNFS_

#include "fnFS.h"
#include "../TNFSlib/tnfslib.h"


class FileSystemTNFS : public FileSystem
{
private:
    tnfsMountInfo _mountinfo;
    unsigned long _last_dns_refresh;
    char _current_dirpath[TNFS_MAX_FILELEN];

public:
    FileSystemTNFS();
    ~FileSystemTNFS();

    bool start(const char *host, uint16_t port=TNFS_DEFAULT_PORT, const char * mountpath=nullptr, const char * userid=nullptr, const char * password=nullptr);

    fsType type() override { return FSTYPE_TNFS; };
    const char * typestring() override { return type_to_string(FSTYPE_TNFS); };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;

    bool exists(const char* path) override;

    bool remove(const char* path) override;

    bool rename(const char* pathFrom, const char* pathTo) override;

    bool dir_open(const char * path) override;
    fsdir_entry *dir_read() override;
    void dir_close();
};

#endif // _FN_FSTNFS_