#ifndef FN_FSFTP_H
#define FN_FSFTP_H

#include <cstddef>
#include <memory>
#include <stdint.h>

#include "peoples_url_parser.h"
#include "fnFTP.h"
#include "fnFS.h"
#include "fnDirCache.h"


class FileSystemFTP : public FileSystem
{
private:
    // parsed FTP URL
    std::unique_ptr<PeoplesUrlParser> _url;

    // FTP client
    fnFTP *_ftp;

    // directory cache
    std::string _username;
    std::string _password;

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
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

    bool exists(const char *path) override;

    bool remove(const char *path) override;

    bool rename(const char *pathFrom, const char *pathTo) override;

    bool is_dir(const char *path) override;
    bool mkdir(const char* path) override;
    bool rmdir(const char* path) override;
    bool dir_exists(const char* path) override;

    bool dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    bool dir_seek(uint16_t pos) override;

    bool keep_alive();

private:
    bool ensure_connected();  // Check connection and reconnect if needed

public:
#ifndef FNIO_IS_STDIO
    FileHandler *cache_file(const char *path, const char *mode);
#endif

};

#endif // FN_FSFTP_H