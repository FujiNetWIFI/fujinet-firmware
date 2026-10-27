#ifndef FN_FSDIRCACHE_H
#define FN_FSDIRCACHE_H

#include "fnFS.h"
#include "fnDirCache.h"

// A FileSystem whose directory listing is read in full into a DirCache by fill_dircache()
class FileSystemDirCache : public FileSystem
{
private:
    // Directory the cache holds, if any
    char _last_dir[MAX_PATHLEN] = { '\0' };
    bool _have_listing = false;

protected:
    DirCache _dircache;

    // Add every entry of path to _dircache; the cache is dropped on error
    virtual success_is_true fill_dircache(const char *path) = 0;

    // Whether the filesystem can list right now
    virtual success_is_true ready_to_list() { RETURN_SUCCESS_IF(_started); }

    // Whether the cache outlives dir_close(), so opening the same path again skips fill_dircache()
    virtual bool keeps_listing() { return true; }

public:
    FileSystemDirCache() = default;
    // Another handle on the same filesystem, with a listing of its own
    FileSystemDirCache(const FileSystemDirCache &other) : FileSystem(other) {}

    success_is_true dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry *dir_read() override { return _dircache.read(); }
    void dir_close() override;
    uint16_t dir_tell() override { return _dircache.tell(); }
    success_is_true dir_seek(uint16_t pos) override { return _dircache.seek(pos); }
};

#endif // FN_FSDIRCACHE_H
