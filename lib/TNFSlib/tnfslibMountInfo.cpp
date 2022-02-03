
#include "tnfslibMountInfo.h"


tnfsMountInfo::tnfsMountInfo(const char *host_name, uint16_t host_port)
{
    strlcpy(hostname, host_name, sizeof(hostname));
    port = host_port;
}

tnfsMountInfo::tnfsMountInfo(in_addr_t host_address, uint16_t host_port)
{
    host_ip = host_address;
    port = host_port;
}

// Make sure to clean up any memory we allocated
tnfsMountInfo::~tnfsMountInfo()
{
    // Find a matching tnfsFileHandleInfo
    for (int i = 0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if (_file_handles[i] != nullptr)
            delete _file_handles[i];
    }
    // Delete any remaining directory cache entries
    empty_dircache();
}

// Empty the current contents of the directory cache
void tnfsMountInfo::empty_dircache()
{
    for (int i = 0; i < TNFS_MAX_DIRCACHE_ENTRIES; i++)
    {
        if (_dir_cache[i] != nullptr)
        {
            delete _dir_cache[i];
            _dir_cache[i] = nullptr;
        }
    }
    _dir_cache_current = 0;
    _dir_cache_count = 0;
    _dir_cache_eof = false;
}

/*
 Add a new entry to the directory cache and return a pointer to it
 Null is returned if we've reached TNFS_MAX_DIRCACHE_ENTRIES
*/
tnfsDirCacheEntry * tnfsMountInfo::new_dircache_entry()
{
    if(_dir_cache_count >= TNFS_MAX_DIRCACHE_ENTRIES)
        return nullptr;

    _dir_cache[_dir_cache_count] = new tnfsDirCacheEntry();
    _dir_cache_count ++;
    return _dir_cache[_dir_cache_count - 1];
}

/*
 Return a pointer to the next unread directory cache entry
 Returns null if there are no cache entries left to read
*/
tnfsDirCacheEntry * tnfsMountInfo::next_dircache_entry()
{
    if(_dir_cache_current >= _dir_cache_count)
        return nullptr;

    _dir_cache_current++;
    return _dir_cache[_dir_cache_current - 1];
}

/*
 Returns the directory position of the currently cached directory
 entry as provided by the server.
 Returns -1 if there is no currently cached entry.
*/
int tnfsMountInfo::tell_dircache_entry()
{
    if(_dir_cache_current >= _dir_cache_count)
        return -1;
    return _dir_cache[_dir_cache_current]->dirpos;
}

/*
 Returns a pointer to the tnfsFileHandleInfo with a matching file handle,
 or null if no match exists in the table.
*/
tnfsFileHandleInfo *tnfsMountInfo::get_filehandleinfo(uint8_t filehandle)
{
    // Find a matching tnfsFileHandleInfo
    for (int i = 0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if (_file_handles[i] != nullptr)
        {
            if (_file_handles[i]->handle_id == filehandle)
                return _file_handles[i];
        }
    }
    return nullptr;
}

/*
 Returns a pointer to a new tnfsFileHandleInfo pointer or null if table is full
*/
tnfsFileHandleInfo *tnfsMountInfo::new_filehandleinfo()
{
    // Find a free slot and create a new tnfsFileHandleInfo
    for (int i = 0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if (_file_handles[i] == nullptr)
        {
            tnfsFileHandleInfo *p = new tnfsFileHandleInfo;
            if (p != nullptr)
            {
                _file_handles[i] = p;
                return p;
            }
        }
    }
    return nullptr;
}

/*
 Removes any existing tnfsFileHandleInfo with a matching file handle
*/
void tnfsMountInfo::delete_filehandleinfo(uint8_t filehandle)
{
    // Find a matching tnfsFileHandleInfo
    for (int i = 0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if (_file_handles[i] != nullptr)
        {
            if (_file_handles[i]->handle_id == filehandle)
            {
                delete _file_handles[i];
                _file_handles[i] = nullptr;
            }
        }
    }
}

/*
 Removes any existing tnfsFileHandleInfo with a matching pointer
*/
void tnfsMountInfo::delete_filehandleinfo(tnfsFileHandleInfo *pFilehandle)
{
    // Find a matching tnfsFileHandleInfo
    for (int i = 0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if (_file_handles[i] == pFilehandle)
        {
            delete _file_handles[i];
            _file_handles[i] = nullptr;
        }
    }
}
