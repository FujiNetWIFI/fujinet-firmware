#ifndef _FN_FSTNFS_
#define _FN_FSTNFS_

#include "fnFS.h"
#include "tnfslib.h"
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif /* ESP_PLATFORM */

class FileSystemTNFS : public FileSystem
{
private:
    tnfsMountInfo _mountinfo;
#ifdef ESP_PLATFORM
    unsigned long _last_dns_refresh  = 0;
    esp_timer_handle_t keepAliveTimerHandle = nullptr;
#else
    uint64_t _last_dns_refresh  = 0;
#endif
    char _current_dirpath[TNFS_MAX_FILELEN];

public:
    FileSystemTNFS();
    ~FileSystemTNFS();

    bool start(const char *host, uint16_t port=TNFS_DEFAULT_PORT, const char * mountpath=nullptr, const char * userid=nullptr, const char * password=nullptr);

    fsType type() override { return FSTYPE_TNFS; };
    const char * typestring() override { return type_to_string(FSTYPE_TNFS); };

    FILE * file_open(const char* path, const char* mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler * filehandler_open(const char* path, const char* mode = FILE_READ) override;
#endif

    bool is_started();
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
};

extern FileSystemTNFS fnTNFS;

#ifdef ESP_PLATFORM
void keepAliveTNFS(void *info);
#endif

#endif // _FN_FSTNFS_
