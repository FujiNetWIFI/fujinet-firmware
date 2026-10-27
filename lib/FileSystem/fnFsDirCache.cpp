#include "fnFsDirCache.h"

#include <cstring>
#include "compat_string.h"

#include "../../include/debug.h"

success_is_true FileSystemDirCache::dir_open(const char *path, const char *pattern, uint16_t diropts)
{
    Debug_printf("FileSystemDirCache::dir_open(\"%s\", \"%s\", %u)\n", path ? path : "", pattern ? pattern : "", diropts);

    if (ready_to_list().is_error() || path == nullptr)
        RETURN_ERROR_AS_FALSE();

    if (_have_listing && keeps_listing() && strcmp(_last_dir, path) == 0)
    {
        Debug_printf("Use directory cache\n");
    }
    else
    {
        Debug_printf("Fill directory cache\n");

        _dircache.clear();
        _have_listing = false;

        if (fill_dircache(path).is_error())
        {
            _dircache.clear();
            RETURN_ERROR_AS_FALSE();
        }

        strlcpy(_last_dir, path, sizeof(_last_dir));
        _have_listing = true;
    }

    // Apply pattern matching filter and sort entries
    RETURN_SUCCESS_IF(_dircache.apply_filter(pattern, diropts).is_success());
}

void FileSystemDirCache::dir_close()
{
    if (!keeps_listing())
    {
        _dircache.clear();
        _have_listing = false;
    }
}
