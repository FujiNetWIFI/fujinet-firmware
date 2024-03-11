#ifndef FN_FSSMB_H
#define FN_FSSMB_H

#include <stdint.h>
#include <cstddef>
#include <smb2/libsmb2.h>

#include "fnFS.h"
#include "fnDirCache.h"


class FileSystemSMB : public FileSystem
{
private:
    struct smb2_context *_smb;
    struct smb2_url *_url;

    // directory cache
    char _last_dir[MAX_PATHLEN];
    DirCache _dircache;

public:
    FileSystemSMB();
    ~FileSystemSMB();

    bool start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_SMB; };
    const char *typestring() override { return type_to_string(FSTYPE_SMB); };

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
};

#endif // FN_FSSMB_H