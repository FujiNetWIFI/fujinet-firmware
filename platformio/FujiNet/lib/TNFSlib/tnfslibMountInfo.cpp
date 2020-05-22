#include "tnfslibMountInfo.h"

// Make sure to clean up any memory we allocated
tnfsMountInfo::~tnfsMountInfo()
{
    // Find a matching tnfsFileHandleInfo
    for(int i=0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if(_file_handles[i] != nullptr)
            delete _file_handles[i];
    }
}

/*
 Returns a pointer to the tnfsFileHandleInfo with a matching file handle,
 or null if no match exists in the table.
*/
tnfsFileHandleInfo * tnfsMountInfo::get_filehandleinfo(uint8_t filehandle)
{
    // Find a matching tnfsFileHandleInfo
    for(int i=0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if(_file_handles[i] != nullptr)
        {
            if(_file_handles[i]->handle_id == filehandle)
                return _file_handles[i];
        }
    }
    return nullptr;
}

/*
 Returns a pointer to a new tnfsFileHandleInfo pointer or null if table is full
*/
tnfsFileHandleInfo * tnfsMountInfo::new_filehandleinfo()
{
    // Find a free slot and create a new tnfsFileHandleInfo
    for(int i=0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if(_file_handles[i] == nullptr)
        {
            tnfsFileHandleInfo *p = new tnfsFileHandleInfo;
            if(p != nullptr)
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
    for(int i=0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if(_file_handles[i] != nullptr)
        {
            if(_file_handles[i]->handle_id == filehandle)
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
void tnfsMountInfo::delete_filehandleinfo(tnfsFileHandleInfo * pFilehandle)
{
    // Find a matching tnfsFileHandleInfo
    for(int i=0; i < TNFS_MAX_FILE_HANDLES; i++)
    {
        if(_file_handles[i] == pFilehandle)
        {
            delete _file_handles[i];
            _file_handles[i] = nullptr;
        }
    }
}
