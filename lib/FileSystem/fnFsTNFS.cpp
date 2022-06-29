#include "fnFsTNFS.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnDNS.h"

#include "fnFsTNFSvfs.h"

FileSystemTNFS::FileSystemTNFS()
{
    // TODO: Maybe allocate space for our TNFS packet so it doesn't have to get put on the stack?
}

FileSystemTNFS::~FileSystemTNFS()
{
    if (_started)
        tnfs_umount(&_mountinfo);
    if(_basepath[0] != '\0')
        vfs_tnfs_unregister(_basepath);
}

bool FileSystemTNFS::start(const char *host, uint16_t port, const char * mountpath, const char * userid, const char * password)
{
    if (_started)
        return false;

    if(host == nullptr || host[0] == '\0')
        return false;
    
    strlcpy(_mountinfo.hostname, host, sizeof(_mountinfo.hostname));

    // Try to resolve the hostname and store that so we don't have to keep looking it up
    _mountinfo.host_ip = get_ip4_addr_by_name(host);
    if(_mountinfo.host_ip == IPADDR_NONE)
    {
        Debug_printf("Failed to resolve hostname \"%s\"\n", host);
        return false;
    }
    // TODO: Refresh the DNS name we resolved after X amount of time
    _last_dns_refresh = fnSystem.millis();

    _mountinfo.port = port;
    _mountinfo.session = TNFS_INVALID_SESSION;

    if(mountpath != nullptr)
        strlcpy(_mountinfo.mountpath, mountpath, sizeof(_mountinfo.mountpath));
    else
        _mountinfo.mountpath[0] = '\0';

    if(userid != nullptr)
        strlcpy(_mountinfo.user, userid, sizeof(_mountinfo.user));
    else
        _mountinfo.user[0] = '\0';

    if(password != nullptr)
        strlcpy(_mountinfo.password, password, sizeof(_mountinfo.password));
    else
        _mountinfo.password[0] = '\0';

    Debug_printf("TNFS mount %s[%s]:%hu\n", _mountinfo.hostname, inet_ntoa(_mountinfo.host_ip), _mountinfo.port);

    int r = tnfs_mount(&_mountinfo);
    if (r != TNFS_RESULT_SUCCESS)
    {
        Debug_printf("TNFS mount failed with code %d\n", r);
        _mountinfo.mountpath[0] = '\0';
        _started = false;
        return false;
    }
    Debug_printf("TNFS mount successful. session: 0x%hx, version: 0x%04hx, min_retry: %hums\n", _mountinfo.session, _mountinfo.server_version, _mountinfo.min_retry_ms);

    // Register a new VFS driver to handle this connection
    if(vfs_tnfs_register(_mountinfo, _basepath, sizeof(_basepath)) != 0)
    {
        Debug_println("Failed to register VFS driver!");
        return false;
    }

    _started = true;

    return true;
}

bool FileSystemTNFS::exists(const char* path)
{
    tnfsStat tstat;

    int result = tnfs_stat(&_mountinfo, &tstat, path);

    return result == TNFS_RESULT_SUCCESS;
}

bool FileSystemTNFS::remove(const char* path)
{
    if(path == nullptr)
        return false;

    // Figure out if this is a file or directory
    tnfsStat tstat;
    if(TNFS_RESULT_SUCCESS != tnfs_stat(&_mountinfo, &tstat, path))
        return false;

    int result;
    if(tstat.isDir)
        result = tnfs_rmdir(&_mountinfo, path);
    else
        result = tnfs_unlink(&_mountinfo, path);

    return result == TNFS_RESULT_SUCCESS;
}

bool FileSystemTNFS::rename(const char* pathFrom, const char* pathTo)
{
    int result = tnfs_rename(&_mountinfo, pathFrom, pathTo);
    return result == TNFS_RESULT_SUCCESS;    
}

FILE * FileSystemTNFS::file_open(const char* path, const char* mode)
{
    if(!_started || path == nullptr)
        return nullptr;

    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    return result;
}

bool FileSystemTNFS::is_dir(const char *path)
{
    char * fpath = _make_fullpath(path);
    struct stat info;
    stat( fpath, &info);
    return (info.st_mode == S_IFDIR) ? true: false;
}

bool FileSystemTNFS::dir_open(const char * path, const char *pattern, uint16_t diropts)
{
    if(!_started)
        return false;
    
    uint8_t d_opt = 0;
    uint8_t s_opt = 0;

    if(diropts & DIR_OPTION_DESCENDING)
        s_opt |= TNFS_DIRSORT_DESCENDING;
    if(diropts & DIR_OPTION_FILEDATE)
        s_opt |= TNFS_DIRSORT_MODIFIED;

    if(TNFS_RESULT_SUCCESS == tnfs_opendirx(&_mountinfo, path, s_opt, d_opt, pattern, 0))
    {
        // Save the directory for later use, making sure it starts and ends with '/''
        if(path[0] != '/')
        {
            _current_dirpath[0] = '/';
            strlcpy(_current_dirpath + 1, path, sizeof(_current_dirpath)-1);
        } 
        else
        {
            strlcpy(_current_dirpath, path, sizeof(_current_dirpath));
        }
        int l = strlen(_current_dirpath);
        if((l > 0) && (l < sizeof(_current_dirpath) -2) && (_current_dirpath[l -1] != '/'))
        {
            _current_dirpath[l] = '/';
            _current_dirpath[l+1] = '\0';
        }

        return true;
    }

    return false;
}

fsdir_entry * FileSystemTNFS::dir_read()
{
    if(!_started)
        return nullptr;

    tnfsStat fstat;

    _direntry.filename[0] = '\0';
    if(TNFS_RESULT_SUCCESS != tnfs_readdirx(&_mountinfo, &fstat, _direntry.filename, sizeof(_direntry.filename)))
        return nullptr;

    _direntry.size = fstat.filesize;
    _direntry.modified_time = fstat.m_time;
    _direntry.isDir = fstat.isDir;

    return &_direntry;
}

void FileSystemTNFS::dir_close()
{
    if(!_started)
        return;
    tnfs_closedir(&_mountinfo);
    _current_dirpath[0] = '\0';
}

uint16_t FileSystemTNFS::dir_tell()
{
    if(!_started)
        return FNFS_INVALID_DIRPOS;;

    uint16_t position;
    if(0 != tnfs_telldir(&_mountinfo, &position))
        position = FNFS_INVALID_DIRPOS;

    return position;
}

bool FileSystemTNFS::dir_seek(uint16_t position)
{
    if(!_started)
        return false;

    return 0 == tnfs_seekdir(&_mountinfo, position);
}
