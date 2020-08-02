#include "fuji.h"

#include "../FileSystem/fnFS.h"
#include "../FileSystem/fnFsSD.h"
#include "../FileSystem/fnFsTNFS.h"

#include "fujiHost.h"

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

    // Delete the filesystem if it's not one of the global oens
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
        cleanup();
        break;
    case HOSTTYPE_TNFS:
        cleanup();
        break;
    }

    _type = type;
}

/* Sets the hostname. If we're initialized and this is a different name
 from what we had, unmount the previous host
 */
void fujiHost::set_hostname(const char * hostname)
{
    if(_type != HOSTTYPE_UNINITIALIZED)
    {
        if(0 == strncasecmp(_hostname, hostname, sizeof(_hostname)))
        {
            Debug_print("fujiHost::set_hostname new name matches old - nothing changes\n");
            return;
        }
        Debug_printf("fujiHost::set_hostname replacing hold host \"%s\"\n", _hostname);
        set_type(HOSTTYPE_UNINITIALIZED);
    }
    strlcpy(_hostname, hostname, sizeof(_hostname));
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
        result = _fs->dir_seek(pos);
        break;
    case HOSTTYPE_UNINITIALIZED:
        break;
    }
    return result;
}

bool fujiHost::dir_open(const char *path)
{
    Debug_printf("::dir_open {%d:%d} \"%s\"\n", slotid, _type, path);
    if (_fs == nullptr)
    {
        Debug_println("::dir_open no FileSystem set");
        return false;
    }

    int result = false;
    switch (_type)
    {
    case HOSTTYPE_LOCAL:
    case HOSTTYPE_TNFS:
        result = _fs->dir_open(path);
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

bool fujiHost::exists(const char *path)
{
    if (_type == HOSTTYPE_UNINITIALIZED || _fs == nullptr)
        return false;

    return _fs->exists(path);
}

bool fujiHost::exists(const string path)
{
    return exists(path.c_str());
}

FILE *fujiHost::open(const char *path, const char *mode)
{
    if (_type == HOSTTYPE_UNINITIALIZED || _fs == nullptr)
        return nullptr;

    return _fs->file_open(path, mode);
}

FILE *fujiHost::open(const string path, const char *mode)
{
    return open(path.c_str(), mode);
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

    // In any other case, unmount whatever we have and start fresh
    set_type(HOSTTYPE_TNFS);

    _fs = new FileSystemTNFS;

    if (_fs == nullptr)
    {
        Debug_println("Couldn't create a new TNFSFS in fujiHost::mount_tnfs!");
    }
    else
    {
        Debug_println("Calling TNFS::begin");
        if (((FileSystemTNFS *)_fs)->start(_hostname))
        {
            return 0;
        }
    }

    return -1;
}

/* Returns true if successful
*  We expect a valid devicename, currently:
*  "SD" = local
*  anything else = TNFS
*/
bool fujiHost::mount()
{
    Debug_printf("::mount {%d} \"%s\"\n", slotid, _hostname);

    // Try mounting locally first
    if (0 == mount_local())
        return true;

    // Try mounting TNFS last
    return 0 == mount_tnfs();
}
