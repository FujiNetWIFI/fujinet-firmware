#include "fnFsTNFS.h"
#include "../TNFSlib/tnfslib.h"
#include "../tcpip/fnDNS.h"
#include "../hardware/fnSystem.h"
#include "../../include/debug.h"
#include "fnFsTNFSvfs.h"

TnfsFileSystem::TnfsFileSystem()
{
    // TODO: Maybe allocate space for our TNFS packet so it doesn't have to get put on the stack?
}

TnfsFileSystem::~TnfsFileSystem()
{
    if (_connected)
        tnfs_umount(&_mountinfo);
    if(_basepath[0] != '\0')
        vfs_tnfs_unregister(_basepath);
}

bool TnfsFileSystem::start(const char *host, uint16_t port, const char * mountpath, const char * userid, const char * password)
{
    if (_connected)
        return false;

    if(host == nullptr || host[0] == '\0')
        return false;
    
    strncpy(_mountinfo.hostname, host, sizeof(_mountinfo.hostname));

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
        strncpy(_mountinfo.mountpath, mountpath, sizeof(_mountinfo.mountpath));
    else
        _mountinfo.mountpath[0] = '\0';

    if(userid != nullptr)
        strncpy(_mountinfo.user, userid, sizeof(_mountinfo.user));
    else
        _mountinfo.user[0] = '\0';

    if(password != nullptr)
        strncpy(_mountinfo.password, password, sizeof(_mountinfo.password));
    else
        _mountinfo.password[0] = '\0';

#ifdef DEBUG
    Debug_printf("TNFS mount %s[%s]:%hu\n", _mountinfo.hostname, inet_ntoa(_mountinfo.host_ip), _mountinfo.port);
#endif

    int r = tnfs_mount(&_mountinfo);
    if (r != TNFS_RESULT_SUCCESS)
    {
#ifdef DEBUG
        Debug_printf("TNFS mount failed with code %d\n", r);
#endif
        _mountinfo.mountpath[0] = '\0';
        _connected = false;
        return false;
    }
#ifdef DEBUG
    Debug_printf("TNFS mount successful. session: 0x%hx, version: 0x%hx, min_retry: %hums\n", _mountinfo.session, _mountinfo.server_version, _mountinfo.min_retry_ms);
#endif

    // Register a new VFS driver to handle this connection
    if(vfs_tnfs_register(_mountinfo, _basepath, sizeof(_basepath)) != 0)
    {
        #ifdef DEBUG
        Debug_println("Failed to register VFS driver!");
        #endif
        return false;
    }

    _connected = true;

    return true;
}

FILE * TnfsFileSystem::file_open(const char* path, const char* mode)
{
    if(!_connected || path == nullptr)
        return nullptr;

    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    return result;
}

bool TnfsFileSystem::dir_open(const char * path)
{
    if(!_connected)
        return false;

    int r = tnfs_opendir(&_mountinfo, path);
    return (r == TNFS_RESULT_SUCCESS);
}

fsdir_entry * TnfsFileSystem::dir_read()
{
    if(!_connected)
        return nullptr;

    // Skip "." and ".."; server returns EINVAL on trying to stat ".."
    bool skip;
    do 
    {
        _direntry.filename[0] = '\0';
        if(TNFS_RESULT_SUCCESS != tnfs_readdir(&_mountinfo, _direntry.filename, sizeof(_direntry.filename)))
            return nullptr;

        skip = (_direntry.filename[0] == '.' && _direntry.filename[1] == '\0') || 
                        (_direntry.filename[0] == '.' && _direntry.filename[1] == '.' && _direntry.filename[2] == '\0');
    } while (skip);

    tnfsStat fstat;
    if(tnfs_stat(&_mountinfo, &fstat, _direntry.filename) == TNFS_RESULT_SUCCESS)
    {
        _direntry.size = fstat.filesize;
        _direntry.modified_time = fstat.m_time;
        _direntry.isDir = fstat.isDir;
        return &_direntry;
    }
    return nullptr;
}

void TnfsFileSystem::dir_close()
{
    if(!_connected)
        return;

    tnfs_closedir(&_mountinfo);
}
