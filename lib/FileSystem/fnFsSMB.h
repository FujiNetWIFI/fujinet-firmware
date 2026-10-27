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

    void _release();

public:
    FileSystemSMB();
    ~FileSystemSMB();

    success_is_true start(const char *url, const char *user=nullptr, const char *password=nullptr);

    fsType type() override { return FSTYPE_SMB; };
    const char *typestring() override { return type_to_string(FSTYPE_SMB); };

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

protected:
    success_is_true fill_dircache(const char *path) override;
};

#endif // FN_FSSMB_H
