#ifndef FN_FSAFP_H
#define FN_FSAFP_H

#include <memory>
#include <string>

#include "fnFS.h"
#include "fnDirCache.h"

#include "afpSession.h"

class FileSystemAFP : public FileSystem
{
private:
    // Shared with every open file, so a file outlives an unmount safely.
    std::shared_ptr<AfpSession> _session;
    // The mounted directory below the volume: empty, or "/dir" with no trailing slash.
    std::string _root;

    // The volume path last listed into _dircache; empty when it holds nothing.
    std::string _last_dir;
    DirCache _dircache;

    // path as lib/afp wants it: below _root, leading slash, no trailing one.
    std::string full_path(const char *path) const;

public:
    FileSystemAFP();
    ~FileSystemAFP();

    // afp://[user[:password]@]host[:port]/Volume[/dir]. user and password are
    // used when the URL has none.
    success_is_true start(const char *url, const char *user = nullptr, const char *password = nullptr);

    fsType type() override { return FSTYPE_AFP; };
    const char *typestring() override { return type_to_string(FSTYPE_AFP); };

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
    bool dir_exists(const char *path) override { return is_dir(path); };

    success_is_true dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    success_is_true dir_seek(uint16_t pos) override;
};

#endif // FN_FSAFP_H
