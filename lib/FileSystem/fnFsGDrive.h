#ifndef FN_FSGDRIVE_H
#define FN_FSGDRIVE_H

#include <cstddef>
#include <stdint.h>
#include <string>
#include <functional>
#include <set>

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

    // Paths opened with write intent that may need uploading back to Drive.
    // Keyed by the (already prefix-resolved) path that file_open() received,
    // which matches what sync_file() is later called with at unmount.
    std::set<std::string> _dirty;

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

    // Upload a locally-written/created image back to Google Drive. Only files
    // that were opened with write intent (tracked in _dirty) are uploaded.
    success_is_true sync_file(const char *path) override;

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

private:
    // Stream a Drive file's content (?alt=media) for the given file id, passing
    // each chunk to sink(). sink returns false to abort. Returns true on a
    // complete, successful download.
    bool stream_download(const std::string &file_id,
                         const std::function<bool(const uint8_t *, int)> &sink);

    // Upload `len` bytes pulled from read_chunk() to Drive at `path` (create or
    // update), then clear the dirty flag on success. Shared by the stdio (FILE*)
    // and FileHandler sync_file paths.
    success_is_true upload_path(const char *path, size_t len,
                                const std::function<int(uint8_t *, int)> &read_chunk);

#ifdef FNIO_IS_STDIO
    // SD-card cache path (relative to SD root) for a Drive file at `path`.
    std::string cache_file_path(const char *path);
#endif
};

#endif // FN_FSGDRIVE_H
