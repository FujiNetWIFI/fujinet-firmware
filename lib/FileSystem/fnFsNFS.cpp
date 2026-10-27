
#include "fnFsNFS.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "compat_string.h"

#include "../../include/debug.h"

#include "nfsc/libnfs.h"
#include "fnFileNFS.h"

FileSystemNFS::FileSystemNFS()
{
    Debug_printf("FileSystemNFS::ctor\n");
    _nfs = nullptr;
    _url = nullptr;
}

FileSystemNFS::~FileSystemNFS()
{
    Debug_printf("FileSystemNFS::dtor\n");
    if (_started)
    {
        nfs_umount(_nfs);
        nfs_destroy_url(_url);
        nfs_destroy_context(_nfs);
    }
}

success_is_true FileSystemNFS::start(const char *url, const char *user, const char *password)
{
    int nfs_error;

    if (_started)
        RETURN_ERROR_AS_FALSE();

    if(url == nullptr || url[0] == '\0')
        RETURN_ERROR_AS_FALSE();

    _nfs = nfs_init_context();
    if (_nfs == nullptr) 
    {
        Debug_printf("FileSystemNFS::start() - failed to init NFS context\n");
        RETURN_ERROR_AS_FALSE();
    }

    _url = nfs_parse_url_full(_nfs, url);
    if (_url == nullptr) 
    {
        Debug_printf("FileSystemNFS::start() - failed to parse URL \"%s\", NFS error: %s\n", url, nfs_get_error(_nfs));
        RETURN_ERROR_AS_FALSE();
    }

    // Set UID/GID if provided
    if (user != nullptr)
    {
        nfs_set_uid(_nfs, atoi(user));
    }
    if (password != nullptr)
    {
        nfs_set_gid(_nfs, atoi(password));
    }

    nfs_error = nfs_mount(_nfs, _url->server, _url->path);

	if (nfs_error != 0) 
    {
        Debug_printf("FileSystemNFS::start() - failed to mount \"%s:%s\", NFS error: %s\n", _url->server, _url->path, nfs_get_error(_nfs));
        RETURN_ERROR_AS_FALSE();
	}

    Debug_printf("NFS share mounted: %s:%s\n", _url->server, _url->path);

    _started = true;

    RETURN_SUCCESS_AS_TRUE();
}

bool FileSystemNFS::exists(const char *path)
{
    struct nfs_stat_64 st;
    int nfs_error = nfs_stat64(_nfs, path, &st);

    return nfs_error == 0;
}

success_is_true FileSystemNFS::remove(const char *path)
{
    if(path == nullptr)
        RETURN_ERROR_AS_FALSE();

    // Figure out if this is a file or directory
    struct nfs_stat_64 st;
    if (0 != nfs_stat64(_nfs, path, &st))
        RETURN_ERROR_AS_FALSE();

    int nfs_error;
    if (S_ISDIR(st.nfs_mode))
        nfs_error = nfs_rmdir(_nfs, path);
    else
        nfs_error = nfs_unlink(_nfs, path);

    if (nfs_error != 0)
        Debug_printf("FileSystemNFS::remove(\"%s\") - failed, NFS error: %s\n", path, nfs_get_error(_nfs));

    RETURN_SUCCESS_IF(nfs_error == 0);
}

success_is_true FileSystemNFS::rename(const char *pathFrom, const char *pathTo)
{
    int nfs_error = nfs_rename(_nfs, pathFrom, pathTo);
    RETURN_SUCCESS_IF(nfs_error == 0);    
}

FILE  *FileSystemNFS::file_open(const char *path, const char *mode)
{
    Debug_printf("FileSystemNFS::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemNFS::filehandler_open(const char *path, const char *mode)
{
    if(!_started || path == nullptr)
        return nullptr;

    // skip '/' at beginning
    const char *nfs_path = path;
    if (nfs_path != nullptr && nfs_path[0] == '/')
        nfs_path += 1;

    struct nfsfh *fh;
    int open_flags = 0;

    // TODO check/fix open mode translation
    for (const char *m = mode; *m != '\0'; m++)
    {
        switch (*m)
        {
        case 'r':
            open_flags = O_RDONLY;
            break;
        case 'w':
            open_flags = O_WRONLY | O_CREAT;
            break;
        case 'a':
            open_flags = O_WRONLY;
            break;
        case '+':
            // TODO
            if (open_flags == O_RDONLY) // "r+""
                open_flags = O_RDWR;
            else if (open_flags == (O_WRONLY | O_CREAT)) // "w+"
                open_flags = O_RDWR | O_CREAT;
            else if (open_flags == O_WRONLY) // "a+"
                open_flags = O_RDWR | O_CREAT;
            break;
        }
    }

    if (nfs_open(_nfs, nfs_path, O_RDONLY, &fh) != 0) // TODO use open_flags
    {
        return nullptr;
    }

    return new FileHandlerNFS(_nfs, fh);
}
#endif

bool FileSystemNFS::is_dir(const char *path)
{
    struct nfs_stat_64 st;
    if (nfs_stat64(_nfs, path, &st) != 0)
        return false;
    return S_ISDIR(st.nfs_mode);
}

success_is_true FileSystemNFS::fill_dircache(const char *path)
{
    // skip '/' at beginning
    const char *nfs_path = path;
    if (nfs_path[0] == '/')
        nfs_path += 1;

    // Open NFS directory
    struct nfsdir *nfs_dir;

    if (nfs_opendir(_nfs, nfs_path, &nfs_dir) != 0)
    {
        Debug_printf("Failed to open directory: %s\n", nfs_get_error(_nfs));
        RETURN_ERROR_AS_FALSE();
    }

    // Populate directory cache with entries
    struct nfsdirent *nfs_de;
    fsdir_entry fs_de;

    while ((nfs_de = nfs_readdir(_nfs, nfs_dir)) != nullptr)
    {
        // skip hidden
        if (nfs_de->name[0] == '.')
            continue;

        // new dir entry
        fs_de = fsdir_entry();

        // set entry members
        strlcpy(fs_de.filename, nfs_de->name, sizeof(fs_de.filename));
        fs_de.isDir = S_ISDIR(nfs_de->mode);
        fs_de.size = (uint32_t)nfs_de->size;
        fs_de.modified_time = (time_t)nfs_de->mtime.tv_sec;

        if (_dircache->add_entry(fs_de).is_error())
        {
            nfs_closedir(_nfs, nfs_dir);
            RETURN_ERROR_AS_FALSE();
        }

        if (fs_de.isDir)
        {
            Debug_printf(" add entry: \"%s\"\tDIR\n", fs_de.filename);
        }
        else
        {
            Debug_printf(" add entry: \"%s\"\t%lu\n", fs_de.filename, fs_de.size);
        }
    }
    nfs_closedir(_nfs, nfs_dir);

    RETURN_SUCCESS_AS_TRUE();
}
