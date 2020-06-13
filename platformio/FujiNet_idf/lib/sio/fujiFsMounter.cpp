#include "fuji.h"

#include "../FileSystem/fnFS.h"
#include "../FileSystem/fnFsSD.h"
#include "../FileSystem/fnFsTNFS.h"

#include "fujiFsMounter.h"


void fujiFsMounter::unmount()
{
    cleanup();
}

/* Perform any cleanup before destruction/reassignment
*/
void fujiFsMounter::cleanup()
{
    if(_fs != nullptr)
        _fs->dir_close();

    // Delete the filesystem if it's not one of the global oens
    if(_fs->is_global() == false)
        delete _fs;

    _fs = nullptr;

    _hostname[0] = '\0';
}

/* Set the type of filesystem we're using, performing any cleanup if needed
*/
void fujiFsMounter::set_type(fujiFSType type)
{
    // Clean-up if we're changing from one type to another
    switch(_type)
    {
    case FNFILESYS_UNINITIALIZED:
        break;
    case FNFILESYS_LOCAL:
        cleanup();
        break;
    case FNFILESYS_TNFS:
        cleanup();
        break;
    }

    _type = type;
}

bool fujiFsMounter::dir_open(const char *path)
{

#ifdef DEBUG
    Debug_printf("::dir_open {%d:%d} \"%s\"\n", slotid, _type, path);
#endif
    if(_fs == nullptr)
    {
#ifdef DEBUG
    Debug_println("::dir_open no FileSystem set");
#endif
        return false;
    }

    int result = false;
    switch(_type)
    {
    case FNFILESYS_LOCAL:
    case FNFILESYS_TNFS:
        result =_fs->dir_open(path);
        break;
    case FNFILESYS_UNINITIALIZED:
        break;        
    }
    return result;
}

fsdir_entry_t * fujiFsMounter::dir_nextfile()
{
#ifdef DEBUG
    Debug_printf("::dir_nextfile {%d:%d}\n", slotid, _type);
#endif

    switch(_type)
    {
    case FNFILESYS_LOCAL:
    case FNFILESYS_TNFS:
        return _fs->dir_read();
    case FNFILESYS_UNINITIALIZED:
        break;
    }

    return nullptr;
}

void fujiFsMounter::dir_close()
{
    if(_type != FNFILESYS_UNINITIALIZED && _fs != nullptr)
        _fs->dir_close();
}


bool fujiFsMounter::exists(const char *path)
{
    if(_type == FNFILESYS_UNINITIALIZED || _fs == nullptr)
        return false;

    return _fs->exists(path);
}

bool fujiFsMounter::exists(const string path)
{
    return exists(path.c_str());
}

FILE * fujiFsMounter::open(const char *path, const char *mode)
{
    if(_type == FNFILESYS_UNINITIALIZED || _fs == nullptr)
        return nullptr;

    return _fs->file_open(path, mode);
}

FILE * fujiFsMounter::open(const string path, const char *mode)
{
    return open(path.c_str(), mode);
}


/* Returns pointer to current hostname or null if no hostname has been assigned
*/
const char* fujiFsMounter::get_hostname(char *buffer, size_t buffersize)
{
    const char *result = NULL;
    switch(_type)
    {
    case FNFILESYS_LOCAL:
        result = _sdhostname;
        break;
    case FNFILESYS_TNFS:
        if(_fs != NULL)
            result = _hostname;
        break;
    case FNFILESYS_UNINITIALIZED:
        break;        
    }

    if(buffer != NULL)
    {
        if(result != NULL)
            strncpy(buffer, result, buffersize);
        else
            if(buffersize > 0)
                buffer[0] = '\0';
    }

    return result;
}

/* Returns pointer to current hostname or null if no hostname has been assigned
*/
const char* fujiFsMounter::get_hostname()
{
    return get_hostname(NULL, 0);
}


/* Returns:
    0 on success
   -1 devicename isn't a local one
*/
int fujiFsMounter::mount_local(const char *devicename)
{
    int result = -1;
#ifdef DEBUG
    Debug_printf("::mount_local Attempting mount of \"%s\"\n", devicename);
#endif

    if(0 == strcmp(_sdhostname, devicename))
    {
        // Don't do anything if that's already what's set
        if(_type == FNFILESYS_LOCAL)
        {
#ifdef DEBUG
        Debug_println("Type is already LOCAL");
#endif        
            return 0;
        }
        // Otherwise set the new type
#ifdef DEBUG
        Debug_println("Setting type to LOCAL");
#endif        
        set_type(FNFILESYS_LOCAL);
        _fs = &fnSDFAT;
        result = 0;
    }
    return result;
}

/* Returns:
    0 on success
   -1 on failure
*/
int fujiFsMounter::mount_tnfs(const char *hostname)
{
#ifdef DEBUG
        Debug_printf("::mount_tnfs {%d:%d} \"%s\"\n", slotid, _type, hostname);
#endif

    // Don't do anything if that's already what's set
    if(_type == FNFILESYS_TNFS)
    {
        if(_fs != NULL && _fs->running());
        {
#ifdef DEBUG
        Debug_printf("::mount_tnfs Currently connected to host \"%s\"\n", _hostname);
#endif
            if(strcmp(_hostname, hostname) == 0)
                return 0;
        }
    }
    // In any other case, unmount whatever we have and start fresh
    set_type(FNFILESYS_TNFS);

    _fs = new FileSystemTNFS;

    if(_fs == nullptr)
    {
#ifdef DEBUG
        Debug_println("Couldn't create a new TNFSFS in fujiFsMounter::mount_tnfs!");
#endif
    }
    else
    {
#ifdef DEBUG
        Debug_println("Calling TNFS::begin");
#endif
        if(((FileSystemTNFS *)_fs)->start(hostname))
        {
            strncpy(_hostname, hostname, sizeof(_hostname));
            return 0;
        }
    }

    return -1;
}

/* Returns true if successful
*  We expect a valid devicename, currently:
*  SD = local
*  anything else = TNFS
*/
bool fujiFsMounter::mount(const char * devicename)
{
#ifdef DEBUG
    Debug_printf("::mount {%d} \"%s\"\n", slotid, devicename);
#endif

    if(devicename == NULL)
        return false;

    // Try mounting locally first
    int result = mount_local(devicename);
    if(result == 0)
        return true;
    // Try mounting TNFS last
    if(result == -1)
    {
        result = mount_tnfs(devicename);
    }
    return (result == 0);
}
