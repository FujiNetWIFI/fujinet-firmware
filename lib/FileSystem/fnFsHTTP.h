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

#include <string>

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

    // Build the fully-qualified, url-encoded URL for a file path under this host.
    std::string make_file_url(const char *path);

public:
    FileSystemHTTP();
    ~FileSystemHTTP();

    success_is_true start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_HTTP; };
    const char *typestring() override { return type_to_string(FSTYPE_HTTP); };

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

    bool exists(const char *path) override;

    success_is_true remove(const char *path) override;

    success_is_true rename(const char *pathFrom, const char *pathTo) override;

    bool is_dir(const char *path) override;
    success_is_true mkdir(const char* path) override { RETURN_ERROR_AS_FALSE(); };
    success_is_true rmdir(const char* path) override { RETURN_ERROR_AS_FALSE(); };
    bool dir_exists(const char* path) override { return true; };


#ifndef FNIO_IS_STDIO
    FileHandler *cache_file(const char *path, const char *mode);
#endif

protected:
    success_is_true fill_dircache(const char *path) override;
};

#endif // FN_FSHTTP_H
