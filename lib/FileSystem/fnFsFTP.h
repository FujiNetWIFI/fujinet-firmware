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

public:
    FileSystemFTP();
    ~FileSystemFTP();

    success_is_true start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_FTP; };
    const char *typestring() override { return type_to_string(FSTYPE_FTP); };

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

    bool exists(const char *path) override;

    success_is_true remove(const char *path) override;

    success_is_true rename(const char *pathFrom, const char *pathTo) override;

    bool is_dir(const char *path) override;
    success_is_true mkdir(const char* path) override;
    success_is_true rmdir(const char* path) override;
    bool dir_exists(const char* path) override;


    success_is_true keep_alive();

private:
    success_is_true ensure_connected();  // Check connection and reconnect if needed

public:
#ifndef FNIO_IS_STDIO
    FileHandler *cache_file(const char *path, const char *mode);
#endif

protected:
    success_is_true fill_dircache(const char *path) override;
    success_is_true ready_to_list() override { return ensure_connected(); }
};

#endif // FN_FSFTP_H
