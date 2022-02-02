
#include "fnFS.h"

#include <esp_vfs.h>

#include <cstring>

// #include "../../include/debug.h"


char * FileSystem::_make_fullpath(const char *path)
{
    if(path != nullptr)
    {
        // Build full path
        char *fullpath = (char *)malloc(MAX_PATHLEN);
        strlcpy(fullpath, _basepath, MAX_PATHLEN);
        // Make sure there's a '/' separating the base from the give path
        if(path[0] != '/')
        {
            int l = strlen(fullpath);
            fullpath[l] = '/';
            fullpath[l+1] = '\0';
        }
        strlcat(fullpath, path, MAX_PATHLEN);
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

// Returns size of file given path
long FileSystem::filesize(const char *filepath)
{
    struct stat fstat;
    if( 0 == stat(filepath, &fstat))
        return fstat.st_size;
    return -1;
}

const char * FileSystem::type_to_string(fsType type)
{
    switch(type)
    {
        case FSTYPE_SPIFFS:
            return "FS_SPIFFS";
        case FSTYPE_SDFAT:
            return "FS_SDFAT";
        case FSTYPE_TNFS:
            return "FS_TNFS";
        default:
            return "UNKNOWN FS TYPE";
    }
}
