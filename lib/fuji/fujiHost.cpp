#include "fujiHost.h"

#include <cstring>
#include "compat_string.h"

#include "../../include/debug.h"

#include "fnFsSD.h"
#include "fnFsTNFS.h"
#include "fnFsSMB.h"
#include "fnFsFTP.h"
#include "fnFsHTTP.h"

#include "utils.h"

void fujiHost::unmount()
{
    cleanup();
}

/* Perform any cleanup before destruction/reassignment
*/
void fujiHost::cleanup()
{
    if (_fs != nullptr)
        _fs->dir_close();

    // Delete the filesystem if it's not one of the global ones
    if (_fs->is_global() == false)
        delete _fs;

    _fs = nullptr;

    _hostname[0] = '\0';
}

/* Set the type of filesystem we're using, performing any cleanup if needed
*/
void fujiHost::set_type(fujiHostType type)
{
    // Clean-up if we're changing from one type to another
    switch (_type)
    {
    case HOSTTYPE_UNINITIALIZED:
        break;
    case HOSTTYPE_LOCAL:
    case HOSTTYPE_TNFS:
    case HOSTTYPE_SMB:
    case HOSTTYPE_FTP:
    case HOSTTYPE_HTTP:
        cleanup();
        break;
    }

    _type = type;
}

/* Sets the hostname. TODO: If we're initialized and this is a different name
 from what we had, unmount the previous host
 */
void fujiHost::set_hostname(const char *hostname)
{
    if (_type != HOSTTYPE_UNINITIALIZED)
    {
        if (0 == strncasecmp(_hostname, hostname, sizeof(_hostname)))
        {
            Debug_print("fujiHost::set_hostname new name matches old - nothing changes\n");
            return;
        }
        Debug_printf("fujiHost::set_hostname \"%s\" replacing old host \"%s\"\n", hostname, _hostname);
        set_type(HOSTTYPE_UNINITIALIZED);
    }
    strlcpy(_hostname, hostname, sizeof(_hostname));
}

// Sets the host slot prefix.
void fujiHost::set_prefix(const char *prefix)
{
    // Clear the prefix if the given one is empty
    if(prefix == nullptr || prefix[0] == '\0')
    {
        Debug_print("fujiHost::set_prefix given empty prefix - clearing current value");
        _prefix[0] = '\0';
        return;
    }

    // If we start with a slash, replace the current prefix, otherwise concatenate
    if(prefix[0] == '\\' || prefix[0] == '/')
    {
        strlcpy(_prefix, prefix, sizeof(_prefix));
    }
    else
    {
        util_concat_paths(_prefix, _prefix, prefix, sizeof(_prefix));
    }

    Debug_printf("fujiHost::set_prefix new prefix = \"%s\"\n", _prefix);
}

uint16_t fujiHost::dir_tell()
{
    Debug_printf("::dir_tell {%d:%d}\n", slotid, _type);
    if (_fs == nullptr)
        return FNFS_INVALID_DIRPOS;

    uint16_t result = FNFS_INVALID_DIRPOS;
    switch (_type)
    {
    case HOSTTYPE_LOCAL:
    case HOSTTYPE_TNFS:
    case HOSTTYPE_SMB:
    case HOSTTYPE_FTP:
    case HOSTTYPE_HTTP:
        result = _fs->dir_tell();
        break;
    case HOSTTYPE_UNINITIALIZED:
        break;
    }
    return result;
}

bool fujiHost::dir_seek(uint16_t pos)
{
    Debug_printf("::dir_seek {%d:%d} %hu\n", slotid, _type, pos);
    if (_fs == nullptr)
        return false;

    bool result = false;
    switch (_type)
    {
    case HOSTTYPE_LOCAL:
    case HOSTTYPE_TNFS:
    case HOSTTYPE_SMB:
    case HOSTTYPE_FTP:
    case HOSTTYPE_HTTP:
        result = _fs->dir_seek(pos);
        break;
    case HOSTTYPE_UNINITIALIZED:
        break;
    }
    return result;
}

bool fujiHost::dir_open(const char *path, const char *pattern, uint16_t options)
{
    Debug_printf("::dir_open {%d:%d} \"%s\", pattern \"%s\"\n", slotid, _type, path, pattern ? pattern : "");
    if (_fs == nullptr)
    {
        Debug_println("::dir_open no FileSystem set");
        return false;
    }

    // Add our prefix before opening
    char realpath[MAX_PATHLEN];
    if( false == util_concat_paths(realpath, _prefix, path, sizeof(realpath)) )
        return false;

    Debug_printf("::dir_open actual path = \"%s\"\n", realpath);

    int result = false;
    switch (_type)
    {
    case HOSTTYPE_LOCAL:
    case HOSTTYPE_TNFS:
    case HOSTTYPE_SMB:
    case HOSTTYPE_FTP:
    case HOSTTYPE_HTTP:
        result = _fs->dir_open(realpath, pattern, options);
        break;
    case HOSTTYPE_UNINITIALIZED:
        break;
    }
    return result;
}

fsdir_entry_t *fujiHost::dir_nextfile()
{
    Debug_printf("::dir_nextfile {%d:%d}\n", slotid, _type);

    switch (_type)
    {
    case HOSTTYPE_LOCAL:
    case HOSTTYPE_TNFS:
    case HOSTTYPE_SMB:
    case HOSTTYPE_FTP:
    case HOSTTYPE_HTTP:
        return _fs->dir_read();
    case HOSTTYPE_UNINITIALIZED:
        break;
    }

    return nullptr;
}

void fujiHost::dir_close()
{
    if (_type != HOSTTYPE_UNINITIALIZED && _fs != nullptr)
        _fs->dir_close();
}

bool fujiHost::file_exists(const char *path)
{
    if (_type == HOSTTYPE_UNINITIALIZED || _fs == nullptr)
        return false;

    // Add our prefix before opening
    char realpath[MAX_PATHLEN];
    if( false == util_concat_paths(realpath, _prefix, path, sizeof(realpath)) )
        return false;

    Debug_printf("::file_exists actual path = \"%s\"\n", realpath);

    return _fs->exists(realpath);
}

long fujiHost::file_size(fnFile *filehandle)
{
    Debug_print("::get_filesize\n");
    if (_type == HOSTTYPE_UNINITIALIZED || _fs == nullptr)
        return -1;
    return _fs->FileSystem::filesize(filehandle);
}

/* If fullpath is given, then the function will fail and return nullptr
   if the combined prefix + path is longer than fullpathlen.
   Fullpath may be the same buffer as path.
*/
fnFile * fujiHost::fnfile_open(const char *path, char *fullpath, int fullpathlen, const char *mode)
{
    if (_type == HOSTTYPE_UNINITIALIZED || _fs == nullptr)
        return nullptr;

    // Add our prefix before opening
    // If given, use fullpathlen as our max buffer size
    int realpathlen = fullpathlen > 0 ? fullpathlen : MAX_PATHLEN;
    char realpath[realpathlen];
    if( false == util_concat_paths(realpath, _prefix, path, realpathlen) )
        return nullptr;

    // If we're given a destination buffer, copy th full path there
    if( fullpath != nullptr )
    {
        if(strlcpy(fullpath, realpath, fullpathlen) != strlen(realpath))
            return nullptr;
    }
    Debug_printf("fujiHost #%d:%s opening file path \"%s\"\n", slotid, _hostname, fullpath);

    return _fs->fnfile_open(fullpath, mode);
}

/* Remove a file from the host
 * Returns true on error, false on success
*/
bool fujiHost::file_remove(char *fullpath)
{
    if (_type == HOSTTYPE_UNINITIALIZED || _fs == nullptr)
        return true;

    return _fs->remove(fullpath);
}

/* Returns pointer to current hostname and, if provided, fills buffer with that string
*/
const char *fujiHost::get_hostname(char *buffer, size_t buffersize)
{
    if (buffer != NULL)
        strlcpy(buffer, _hostname, buffersize);

    return _hostname;
}

/* Returns pointer to current hostname
*/
const char *fujiHost::get_hostname()
{
    return get_hostname(NULL, 0);
}

/* Returns pointer to current basepath
*/
const char *fujiHost::get_basepath()
{
    return _fs->basepath();
}

/* Returns pointer to current hostname and, if provided, fills buffer with that string
*/
const char *fujiHost::get_prefix(char *buffer, size_t buffersize)
{
    if (buffer != NULL)
        strlcpy(buffer, _prefix, buffersize);

    return _prefix;
}

/* Returns pointer to current hostname
*/
const char *fujiHost::get_prefix()
{
    return get_prefix(NULL, 0);
}

/* Returns:
    0 on success
   -1 devicename isn't a local one
*/
int fujiHost::mount_local()
{
    Debug_printf("::mount_local Attempting mount of \"%s\"\n", _hostname);

    if (0 != strcmp(_sdhostname, _hostname))
        return -1;

    // Don't do anything if that's already what's set
    if (_type == HOSTTYPE_LOCAL)
    {
        Debug_println("Type is already LOCAL");
    }
    // Otherwise set the new type
    else
    {
        Debug_println("Setting type to LOCAL");

        set_type(HOSTTYPE_LOCAL);
        _fs = &fnSDFAT;
    }

    return 0;
}

int fujiHost::unmount_local()
{
    // Silently ignore. We can't unregister the SD card.
    return 0;
}

/* Returns:
    0 on success
   -1 on failure
*/
int fujiHost::mount_tnfs()
{
    Debug_printf("::mount_tnfs {%d:%d} \"%s\"\n", slotid, _type, _hostname);

    // Don't do anything if that's already what's set
    if (_type == HOSTTYPE_TNFS)
    {
        if (_fs != nullptr && _fs->running())
        {
            Debug_printf("::mount_tnfs Currently connected to host \"%s\"\n", _hostname);
            return 0;
        }
    }
    else
        set_type(HOSTTYPE_TNFS); // Only start fresh if not HOSTTYPE_TNFS

    _fs = new FileSystemTNFS;

    if (_fs == nullptr)
    {
        Debug_println("Couldn't create a new FileSystemTNFS in fujiHost::mount_tnfs!");
    }
    else
    {
        Debug_printf("Starting FileSystemTNFS(\"%s\")\n", _hostname);
        if (((FileSystemTNFS *)_fs)->start(_hostname))
        {
            return 0;
        }
    }

    return -1;
}

int fujiHost::mount_smb()
{
    Debug_printf("::mount_smb {%d:%d} \"%s\"\n", slotid, _type, _hostname);

    // Don't do anything if that's already what's set
    if (_type == HOSTTYPE_SMB)
    {
        if (_fs != nullptr && _fs->running())
        {
            Debug_printf("::mount_smb Currently connected to share \"%s\"\n", _hostname);
            return 0;
        }
    }
    else
        set_type(HOSTTYPE_SMB); // Only start fresh if not HOSTTYPE_SMB

    _fs = new FileSystemSMB;

    if (_fs == nullptr)
    {
        Debug_println("Couldn't create a new FileSystemSMB in fujiHost::mount_smb!");
    }
    else
    {
        Debug_printf("Starting FileSystemSMB(\"%s\")\n", _hostname);
        // ensure URL starts with lowercase 'smb'
        char url[MAX_HOSTNAME_LEN];
        strcpy(url, _hostname);
        url[0] = 's';
        url[1] = 'm';
        url[2] = 'b';

        if (((FileSystemSMB *)_fs)->start(url))
        {
            return 0;
        }
    }

    return -1;
}

int fujiHost::mount_ftp()
{
    Debug_printf("::mount_ftp {%d:%d} \"%s\"\n", slotid, _type, _hostname);

    // Don't do anything if that's already what's set
    if (_type == HOSTTYPE_FTP)
    {
        if (_fs != nullptr && _fs->running())
        {
            Debug_printf("::mount_ftp Currently connected to host \"%s\"\n", _hostname);
            return 0;
        }
    }
    else
        set_type(HOSTTYPE_FTP); // Only start fresh if not HOSTTYPE_FTP

    _fs = new FileSystemFTP;

    if (_fs == nullptr)
    {
        Debug_println("Couldn't create a new FileSystemFTP in fujiHost::mount_ftp!");
    }
    else
    {
        Debug_printf("Starting FileSystemFTP(\"%s\")\n", _hostname);

        if (((FileSystemFTP *)_fs)->start(_hostname))
        {
            return 0;
        }
    }

    return -1;
}

int fujiHost::mount_http()
{
    Debug_printf("::mount_http {%d:%d} \"%s\"\n", slotid, _type, _hostname);

    // Don't do anything if that's already what's set
    if (_type == HOSTTYPE_HTTP)
    {
        if (_fs != nullptr && _fs->running())
        {
            Debug_printf("::mount_http Currently connected to \"%s\"\n", _hostname);
            return 0;
        }
    }
    else
        set_type(HOSTTYPE_HTTP); // Only start fresh if not HOSTTYPE_HTTP

    _fs = new FileSystemHTTP;

    if (_fs == nullptr)
    {
        Debug_println("Couldn't create a new FileSystemHTTP in fujiHost::mount_http!");
    }
    else
    {
        Debug_printf("Starting FileSystemHTTP(\"%s\")\n", _hostname);

        if (((FileSystemHTTP *)_fs)->start(_hostname))
        {
            return 0;
        }
    }

    return -1;
}

int fujiHost::unmount_fs()
{
    Debug_printf("Filesystem (%s) unmounted.\n", _fs != nullptr ? _fs->typestring() : "null");

    if (_fs != nullptr)
    {
        delete _fs;
        _fs = nullptr;
    }

    return 0;
}

/* Returns true if successful
*  We expect a valid devicename, currently:
*  "SD" = local
*  "smb://" = SMB share
*  "ftp://" = FTP server
*  "http://" or "https://" = Web server with file/dir-like access
*  anything else = TNFS
*/
bool fujiHost::mount()
{
    Debug_printf("::mount {%d} \"%s\"\n", slotid, _hostname);

    if (strlen(_hostname) == 0) {
        Debug_printf("::mount hostname is empty, exiting\r\n");
        return false;
    }

    // Try mounting locally first
    if (0 == mount_local())
        return true;

    if (0 == strncasecmp("smb://", _hostname, 6))
        return 0 == mount_smb();

    if (0 == strncasecmp("ftp://", _hostname, 6))
        return 0 == mount_ftp();

    if (0 == strncasecmp("http://", _hostname, 7) || 0 == strncasecmp("https://", _hostname, 8))
        return 0 == mount_http();

    // Try mounting TNFS last
    return 0 == mount_tnfs();
}

/* Returns true if successful
*/
bool fujiHost::unmount_success()
{
    Debug_printf("::unmount {%d} \"%s\"\n", slotid+1, _hostname);

    if (_type == HOSTTYPE_LOCAL)
    {
        Debug_println("::skip unmounting SD");
        return 0;
    }

    // Try unmounting TNFS/SMB/FTP/HTTP/...
    return 0 == unmount_fs();
}
