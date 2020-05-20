#include "fnFsTNFS.h"
#include "../TNFSlib/tnfslib.h"
#include "../tcpip/fnDNS.h"
#include "../hardware/fnSystem.h"
#include "../../include/debug.h"

TnfsFileSystem::TnfsFileSystem()
{
    // Allocate space for our TNFS packet so it doesn't have to get put on the stack
    // DO OR NO DO?
}

TnfsFileSystem::~TnfsFileSystem()
{
    if (_connected)
        tnfs_umount(_mountinfo);
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
    _last_dns_refresh = fnSystem.millis();

    _mountinfo.port = port;
    _mountinfo.session = 0;

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

    if (tnfs_mount(_mountinfo) == false)
    {
#ifdef DEBUG
        Debug_println("TNFS mount failed");
#endif
        _mountinfo.mountpath[0] = '\0';
        _connected = false;
        return false;
    }
    _connected = true;
#ifdef DEBUG
    Debug_printf("TNFS mount successful. session: 0x%hx, version: 0x%hx, min_retry: %hums\n", _mountinfo.session, _mountinfo.server_version, _mountinfo.min_retry_ms);
#endif
    return true;
}

bool TnfsFileSystem::dir_open(const char * path)
{
    return tnfs_opendir(_mountinfo, path);
}

fsdir_entry * TnfsFileSystem::dir_read()
{
    // Skip "." and ".."; server returns EINVAL on trying to stat ".."
    bool skip;
    do 
    {
        _direntry.filename[0] = '\0';
        if(false == tnfs_readdir(_mountinfo, _direntry.filename, sizeof(_direntry.filename)))
            return nullptr;

        skip = (_direntry.filename[0] == '.' && _direntry.filename[1] == '\0') || 
                        (_direntry.filename[0] == '.' && _direntry.filename[1] == '.' && _direntry.filename[2] == '\0');
    } while (skip);

    tnfsStat fstat;
    if(tnfs_stat(_mountinfo, fstat, _direntry.filename) == 0)
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
    tnfs_closedir(_mountinfo);
}
