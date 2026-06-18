#ifndef FN_FSGDRIVE_H
#define FN_FSGDRIVE_H

#include <cstddef>
#include <stdint.h>
#include <string>

#ifdef ESP_PLATFORM
#include "fnHttpClient.h"
#define GDFS_HTTP_CLIENT fnHttpClient
#else
#include "mgHttpClient.h"
#define GDFS_HTTP_CLIENT mgHttpClient
#endif

#include "GoogleDriveClient.h"
#include "fnFS.h"
#include "fnDirCache.h"

/**
 * FileSystemGDrive
 *
 * Google Drive as a FujiNet *host slot*. A user puts "GDRIVE:///" (optionally
 * with a starting path) in a host slot and can then browse folders and mount
 * media images straight from their Drive.
 *
 * Directory listings come from the Drive REST API; opening a file downloads it
 * in full to the local file cache (memory or SD) and hands back a FileHandler,
 * exactly like the FTP and HTTP host filesystems. All Drive REST + OAuth2 token
 * work is delegated to the shared GoogleDriveClient.
 */
class FileSystemGDrive : public FileSystem
{
private:
    // Shared Google Drive REST + OAuth2 token helper.
    GoogleDriveClient _gdrive;

    // Host-slot URL string ("GDRIVE:///..."), used as the file-cache host key.
    std::string _rawurl;

    // directory cache
    char _last_dir[MAX_PATHLEN];
    DirCache _dircache;

public:
    FileSystemGDrive();
    ~FileSystemGDrive();

    success_is_true start(const char *url, const char *user = nullptr, const char *password = nullptr);

    fsType type() override { return FSTYPE_GDRIVE; };
    const char *typestring() override { return type_to_string(FSTYPE_GDRIVE); };

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

    bool exists(const char *path) override;

    success_is_true remove(const char *path) override;

    success_is_true rename(const char *pathFrom, const char *pathTo) override;

    bool is_dir(const char *path) override;
    success_is_true mkdir(const char *path) override;
    success_is_true rmdir(const char *path) override;
    bool dir_exists(const char *path) override;

    success_is_true dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    success_is_true dir_seek(uint16_t pos) override;

#ifndef FNIO_IS_STDIO
    FileHandler *cache_file(const char *path, const char *mode);
#endif
};

#endif // FN_FSGDRIVE_H
