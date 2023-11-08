#ifndef FN_FSFTP_H
#define FN_FSFTP_H

#include <stdint.h>
#include <cstddef>

#include "EdUrlParser.h"
#include "fnFTP.h"
#include "fnFS.h"
#include "fnDirCache.h"


class FileSystemFTP : public FileSystem
{
private:
    // parsed FTP URL
    EdUrlParser *_url;

    // fnFTP instance
    fnFTP *_ftp;

    // directory cache
    char _last_dir[MAX_PATHLEN];
    DirCache _dircache;

public:
    FileSystemFTP();
    ~FileSystemFTP();

    bool start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_FTP; };
    const char *typestring() override { return type_to_string(FSTYPE_FTP); };

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;

    bool exists(const char *path) override;

    bool remove(const char *path) override;

    bool rename(const char *pathFrom, const char *pathTo) override;

    bool is_dir(const char *path) override;
    bool mkdir(const char* path) override { return true; };
    bool rmdir(const char* path) override { return true; };
    bool dir_exists(const char* path) override { return true; };

    bool dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t pos) override;

    FileHandler *cache_file(const char *path);

protected:
    bool isValidURL(EdUrlParser *url);
};

#endif // FN_FSFTP_H