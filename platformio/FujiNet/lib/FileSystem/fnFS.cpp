#include <esp_vfs.h>
#include "esp_vfs_fat.h"

#include "fnFS.h"
#include "../../include/debug.h"


char * FileSystem::_make_fullpath(const char *path)
{
    if(path != nullptr)
    {
        // Build full path
        char *fullpath = (char *)malloc(MAX_PATHLEN);
        strncpy(fullpath, _basepath, MAX_PATHLEN);
        // Make sure there's a '/' separating the base from the give path
        if(path[0] != '/')
        {
            int l = strlen(fullpath);
            fullpath[l] = '/';
            fullpath[l+1] = '\0';
        }
        strncat(fullpath, path, MAX_PATHLEN);
        #ifdef DEBUG
        //Debug_printf("_make_fullpath \"%s\" -> \"%s\"\n", path, fullpath);
        #endif

        return fullpath;
    }
    return nullptr;
}

// Returns size of open file
long FileSystem::filesize(FILE *f)
{
    long curr = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, curr, SEEK_SET);
    return end;
}