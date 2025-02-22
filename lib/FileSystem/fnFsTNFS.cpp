#include "fnFsTNFS.h"
#include "fnFileLocal.h"

#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

#ifdef ESP_PLATFORM
#include "fnFsTNFSvfs.h"
#else
#include "fnFileTNFS.h"
#endif

#include "fnSystem.h"
#include "fnDNS.h"
#include "tnfslib.h"
#include "compat_string.h"
#include "../../include/debug.h"

FileSystemTNFS fnTNFS;

FileSystemTNFS::FileSystemTNFS()
{
    // TODO: Maybe allocate space for our TNFS packet so it doesn't have to get put on the stack?
}

FileSystemTNFS::~FileSystemTNFS()
{
    if (_started)
        tnfs_umount(&_mountinfo);
#ifdef ESP_PLATFORM
    if(_basepath[0] != '\0')
    vfs_tnfs_unregister(_basepath);

    if (keepAliveTimerHandle != nullptr)
    {
        esp_timer_stop(keepAliveTimerHandle);
        esp_timer_delete(keepAliveTimerHandle);
        keepAliveTimerHandle = nullptr;
    }
#endif
}

bool FileSystemTNFS::start(const char *host, uint16_t port, const char * mountpath, const char * userid, const char * password)
{
    if (_started)
        return false;

    if(host == nullptr)
        return false;

    const char *host_no_prefix;
    if (strncmp("_tcp.", host, 5) == 0)
    {
        host_no_prefix = &host[5];
        _mountinfo.protocol = TNFS_PROTOCOL_TCP;
    }
    else if (strncmp("_udp.", host, 5) == 0)
    {
        host_no_prefix = &host[5];
        _mountinfo.protocol = TNFS_PROTOCOL_UDP;
    }
    else
    {
        host_no_prefix = host;
    }
    if (host_no_prefix[0] == '\0')
    {
            return false;
    }
    strlcpy(_mountinfo.hostname, host_no_prefix, sizeof(_mountinfo.hostname));

    // Try to resolve the hostname and store that so we don't have to keep looking it up
    _mountinfo.host_ip = get_ip4_addr_by_name(_mountinfo.hostname);
    if(_mountinfo.host_ip == IPADDR_NONE)
    {
        Debug_printf("Failed to resolve hostname \"%s\"\r\n", _mountinfo.hostname);
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

    Debug_printf("TNFS mount %s[%s]:%hu\r\n", _mountinfo.hostname, compat_inet_ntoa(_mountinfo.host_ip), _mountinfo.port);

    int r = tnfs_mount(&_mountinfo);
    if (r != TNFS_RESULT_SUCCESS)
    {
        Debug_printf("TNFS mount failed with code %d\r\n", r);
        _mountinfo.mountpath[0] = '\0';
        _started = false;
        return false;
    }
    Debug_printf("TNFS mount successful. session: 0x%hx, version: 0x%04hx, min_retry: %hums\r\n", _mountinfo.session, _mountinfo.server_version, _mountinfo.min_retry_ms);

#ifdef ESP_PLATFORM
    // Register a new VFS driver to handle this connection
    if(vfs_tnfs_register(_mountinfo, _basepath, sizeof(_basepath)) != 0)
    {
        Debug_println("Failed to register VFS driver!");
        return false;
    }

    esp_timer_create_args_t tcfg = {
        .callback = keepAliveTNFS,
        .arg = this,
        .dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK,
        .name = "tnfs_keep_alive",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&tcfg, &keepAliveTimerHandle);
    // Send a keep-alive message every 60s.
    esp_timer_start_periodic(keepAliveTimerHandle, 60 * 1000000);
#endif

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
#ifdef ESP_PLATFORM
    if(!_started || path == nullptr)
        return nullptr;

    char * fpath = _make_fullpath(path);
    FILE * result = fopen(fpath, mode);
    free(fpath);
    return result;
#else
    Debug_printf("FileSystemTNFS::file_open() - ERROR! Use filehandler_open() instead\n");
    return nullptr;
#endif
}

bool FileSystemTNFS::is_dir(const char *path)
{
#ifdef ESP_PLATFORM
    char * fpath = _make_fullpath(path);
    struct stat info;
    stat( fpath, &info);
    return (info.st_mode == S_IFDIR) ? true: false;
#else
    tnfsStat tstat;
    int result = tnfs_stat(&_mountinfo, &tstat, path);
    return tstat.isDir ? true : false;
#endif
}

#ifndef FNIO_IS_STDIO
FileHandler * FileSystemTNFS::filehandler_open(const char* path, const char* mode)
{
#ifdef ESP_PLATFORM
    FILE * fh = file_open(path, mode);
    return (fh == nullptr) ? nullptr : new FileHandlerLocal(fh);
#else
    if(!_started || path == nullptr)
        return nullptr;

    int16_t handle;
    uint16_t create_perms = TNFS_CREATEPERM_S_IRUSR | TNFS_CREATEPERM_S_IWUSR | TNFS_CREATEPERM_S_IXUSR;

    // Translate mode to open_mode
    uint16_t open_mode = 0;
    for (const char *m = mode; *m != '\0'; m++)
    {
        switch (*m)
        {
        case 'r':
            open_mode = TNFS_OPENMODE_READ;
            break;
        case 'w':
            open_mode = TNFS_OPENMODE_WRITE | TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE_TRUNCATE;
            break;
        case 'a':
            open_mode = TNFS_OPENMODE_WRITE | TNFS_OPENMODE_WRITE_CREATE;
            break;
        case '+':
            if (open_mode == TNFS_OPENMODE_READ) // "r+""
                open_mode = TNFS_OPENMODE_READWRITE;
            else if (open_mode == (TNFS_OPENMODE_WRITE | TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE_TRUNCATE)) // "w+"
                open_mode = TNFS_OPENMODE_READWRITE | TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE_TRUNCATE;
            else if (open_mode == (TNFS_OPENMODE_WRITE | TNFS_OPENMODE_WRITE_CREATE)) // "a+"
                open_mode = TNFS_OPENMODE_READWRITE | TNFS_OPENMODE_WRITE_CREATE;
            break;
        }
    }
    if (open_mode == 0)
    {
        Debug_printf("FileSystemTNFS::filehandler_open - bad open mode %s -> %u\n", mode, open_mode);
        return nullptr;
    }

    int result = tnfs_open(&_mountinfo, path, open_mode, create_perms, &handle);
    if(result != TNFS_RESULT_SUCCESS)
    {
        #ifdef DEBUG
        //Debug_printf("vfs_tnfs_open = %d\n", result);
        #endif
        errno = tnfs_code_to_errno(result);
        return nullptr;
    }
    errno = 0;
    return new FileHandlerTNFS(&_mountinfo, handle);
#endif
}
#endif

bool FileSystemTNFS::dir_open(const char * path, const char *pattern, uint16_t diropts)
{
    if(!_started)
        return false;

    uint8_t d_opt = 0;
    uint8_t s_opt = 0;
    char realpat[TNFS_MAX_FILELEN];
    char *thepat = 0;

    if (!!pattern) {
        thepat = realpat;
        if (pattern[0] == '!')
        {
            snprintf(realpat, sizeof(realpat), "**/%s*", pattern+1);
            d_opt |= TNFS_DIROPT_NO_FOLDERS;
            d_opt |= TNFS_DIROPT_TRAVERSE;
        }
        else
        {
            strlcpy (realpat, pattern, sizeof (realpat));
            if (realpat[strlen(realpat)-1] == '/') {
                Debug_print (
                    "FileSystemTNFS::dir_open applying pattern to directories\n"
                );
                realpat[strlen(realpat)-1] = '\0';
                d_opt |= TNFS_DIROPT_DIR_PATTERN;
            }
        }
    }
    if(diropts & DIR_OPTION_DESCENDING)
        s_opt |= TNFS_DIRSORT_DESCENDING;
    if(diropts & DIR_OPTION_FILEDATE)
        s_opt |= TNFS_DIRSORT_MODIFIED;

    if(TNFS_RESULT_SUCCESS == tnfs_opendirx(&_mountinfo, path, s_opt, d_opt, thepat, 0))
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

#ifdef ESP_PLATFORM
void keepAliveTNFS(void *info)
{
#ifdef VERBOSE_TNFS
    Debug_println("Sending keep-alive command");
#endif
    FileSystemTNFS *parent = (FileSystemTNFS *)info;
    parent->exists("keep-alive");
}
#endif
