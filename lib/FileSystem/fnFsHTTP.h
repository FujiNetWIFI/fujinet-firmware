#ifndef FN_FSHTTP_H
#define FN_FSHTTP_H

#include <cstddef>
#include <memory>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "fnHttpClient.h"
#define HTTP_CLIENT_CLASS fnHttpClient
#else
#include "mgHttpClient.h"
#define HTTP_CLIENT_CLASS mgHttpClient
#endif

#include "peoples_url_parser.h"
#include "fnFS.h"
#include "fnDirCache.h"
#include "IndexParser.h"


class FileSystemHTTP : public FileSystem
{
private:
    // parsed HTTP URL
    std::unique_ptr<PeoplesUrlParser> _url;

    // HTTP client
    HTTP_CLIENT_CLASS *_http;

    // directory index parser
    IndexParser _parser;

    // directory cache
    char _last_dir[MAX_PATHLEN];
    DirCache _dircache;

public:
    FileSystemHTTP();
    ~FileSystemHTTP();

    bool start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_HTTP; };
    const char *typestring() override { return type_to_string(FSTYPE_HTTP); };

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

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

#ifndef FNIO_IS_STDIO
    FileHandler *cache_file(const char *path, const char *mode);
#endif

};

#endif // FN_FSHTTP_H