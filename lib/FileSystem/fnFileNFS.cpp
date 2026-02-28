
#include <errno.h>

#include "fnFileNFS.h"
#include "../../include/debug.h"


FileHandlerNFS::FileHandlerNFS(struct nfs_context *nfs, struct nfsfh *handle)
{
    Debug_println("new FileHandlerNFS");
    _nfs = nfs;
    _handle = handle;
};


FileHandlerNFS::~FileHandlerNFS()
{
    Debug_println("delete FileHandlerNFS");
    if (_handle != nullptr) close(false);
}


int FileHandlerNFS::close(bool destroy)
{
    Debug_println("FileHandlerNFS::close");
    int result = 0;
    if (_handle != nullptr) 
    {
        result = nfs_close(_nfs, _handle);
        _handle = nullptr;
        _nfs = nullptr;
    }
    if (destroy) delete this;
    return result;
}


int FileHandlerNFS::seek(long int off, int whence)
{
    Debug_println("FileHandlerNFS::seek");
    uint64_t new_pos;
    if (nfs_lseek(_nfs, _handle, off, whence, &new_pos) < 0)
    {
        Debug_printf("%s\n", nfs_get_error(_nfs));
        return -1;
    }
    Debug_printf("new pos is %llu\n", new_pos);
    return 0;
}


long int FileHandlerNFS::tell()
{
    Debug_println("FileHandlerNFS::tell");
    uint64_t pos;
    if (nfs_lseek(_nfs, _handle, 0, SEEK_CUR, &pos) < 0)
    {
        Debug_printf("%s\n", nfs_get_error(_nfs));
        return -1;
    }
    return (long)pos;
}


size_t FileHandlerNFS::read(void *ptr, size_t size, size_t count)
{
    Debug_println("FileHandlerNFS::read");

    size_t bytes_remaining = size * count;
    size_t bytes_read = 0;
    int result;
    while (bytes_remaining > 0)
    {
        result = nfs_read(_nfs, _handle, (uint8_t *)ptr, (uint32_t)bytes_remaining);
        if (result < 0)
        {
            if (errno == EAGAIN)
                continue;
            else
            {
                Debug_printf("%s\n", nfs_get_error(_nfs));
                break;
            }
        }
        else if (result == 0)
        {
            break; // EOF
        }
        else
        {
            bytes_read += result;
            bytes_remaining -= result;
        }
    }

    return (size_t)(size * count == bytes_read ? count : bytes_read / size);
}


size_t FileHandlerNFS::write(const void *ptr, size_t size, size_t count)
{
    Debug_println("FileHandlerNFS::write");

    size_t bytes_remaining = size * count;
    size_t bytes_written = 0;
    int result;
    while (bytes_remaining > 0)
    {
        result = nfs_write(_nfs, _handle, (uint8_t *)ptr, (uint32_t)bytes_remaining);
        if (result < 0)
        {
            if (errno == EAGAIN)
                continue;
            else
            {
                Debug_printf("%s\n", nfs_get_error(_nfs));
                break;
            }
        }
        else
        {
            bytes_written += result;
            bytes_remaining -= result;
        }
    }

    return (size_t)(size * count == bytes_written ? count : bytes_written / size);
}


int FileHandlerNFS::flush()
{
    Debug_println("FileHandlerNFS::flush");
    int result;
    if ((result = nfs_fsync(_nfs, _handle)) != 0)
    {
        Debug_printf("%s\n", nfs_get_error(_nfs));
        return -1;
    }
    return 0;
}
