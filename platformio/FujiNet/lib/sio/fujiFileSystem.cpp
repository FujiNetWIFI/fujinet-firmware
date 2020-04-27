#include "fuji.h"
#include "fujiFileSystem.h"


void fujiFileSystem::unmount()
{
    cleanup();
}

/* Perform any cleanup before destruction/reassignment
*/
void fujiFileSystem::cleanup()
{
    _dir.close();
    if(_tnfs != NULL)
    {
        if(_tnfs->isConnected())
            _tnfs->end();
    }
    delete _tnfs;
    _hostname[0] = '\0';
    _fs = NULL;
}

/* Set the type of filesystem we're using, performing any cleanup if needed
*/
void fujiFileSystem::set_type(fujiFSType type)
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

int fujiFileSystem::dir_open(const char *path)
{

    int result = -1;
#ifdef DEBUG
    Debug_printf("::dir_open {%d:%d} \"%s\"\n", slotid, _type, path);
#endif
    if(_fs == NULL)
    {
#ifdef DEBUG
    Debug_println("::dir_open no FS*");
#endif
        return -1;
    }

    switch(_type)
    {
    case FNFILESYS_LOCAL:
    case FNFILESYS_TNFS:
        _dir = _fs->open(path, "r");
        result = _dir ? 0 : -1;
        break;
    case FNFILESYS_UNINITIALIZED:
        break;        
    }
    return result;
}

File fujiFileSystem::dir_nextfile()
{
#ifdef DEBUG
    Debug_printf("::dir_nextfile {%d:%d}\n", slotid, _type);
#endif

    switch(_type)
    {
    case FNFILESYS_LOCAL:
        return _dir.openNextFile();;
    case FNFILESYS_TNFS:
        return _dir.openNextFile();;
    case FNFILESYS_UNINITIALIZED:
        break;
    }

    return File();
}

void fujiFileSystem::dir_close()
{
    _dir.close();
}


bool fujiFileSystem::exists(const char *path)
{
    if(_type == FNFILESYS_UNINITIALIZED || _fs == NULL)
        return false;

    return _fs->exists(path);
}

bool fujiFileSystem::exists(const String &path)
{
    if(_type == FNFILESYS_UNINITIALIZED || _fs == NULL)
        return false;

    return _fs->exists(path);
}

fs::File fujiFileSystem::open(const char *path, const char *mode)
{
    if(_type == FNFILESYS_UNINITIALIZED || _fs == NULL)
        return File();

    return _fs->open(path, mode);
}

fs::File fujiFileSystem::open(const String &path, const char *mode)
{
    if(_type == FNFILESYS_UNINITIALIZED || _fs == NULL)
        return File();
    return _fs->open(path, mode);
}


/* Returns pointer to current hostname or null if no hostname has been assigned
*/
const char* fujiFileSystem::get_hostname(char *buffer, size_t buffersize)
{
    const char *result = NULL;
    switch(_type)
    {
    case FNFILESYS_LOCAL:
        result = _sdhostname;
        break;
    case FNFILESYS_TNFS:
        if(_tnfs != NULL)
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
const char* fujiFileSystem::get_hostname()
{
    return get_hostname(NULL, 0);
}


/* Returns:
    0 on success
   -1 devicename isn't a local one
*/
int fujiFileSystem::mount_local(const char *devicename)
{
    int result = -1;

    if(strncmp(_sdhostname, devicename, sizeof(_sdhostname) == 0))
    {
        // Don't do anything if that's already what's set
        if(_type == FNFILESYS_LOCAL)
            return 0;
        // Otherwise set the new type
        set_type(FNFILESYS_LOCAL);
        _fs = &SD;
    }
    return result;
}

/* Returns:
    0 on success
   -1 on failure
*/
int fujiFileSystem::mount_tnfs(const char *hostname)
{
#ifdef DEBUG
        Debug_printf("::mount_tnfs {%d:%d} \"%s\"\n", slotid, _type, hostname);
#endif

    // Don't do anything if that's already what's set
    if(_type == FNFILESYS_TNFS)
    {
        if(_tnfs != NULL && _tnfs->isConnected())
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

    _tnfs = new TNFSFS();
    if(_tnfs == NULL)
    {
#ifdef DEBUG
        Debug_println("Couldn't create a new TNFSFS in fujiFileSystem::mount_tnfs!");
#endif
    }
    else
    {
        _fs = _tnfs;
#ifdef DEBUG
        Debug_println("Calling TNFS::begin");
#endif
        if(_tnfs->begin(hostname, TNFS_PORT))
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
bool fujiFileSystem::mount(const char * devicename)
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
