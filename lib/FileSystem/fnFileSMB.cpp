
#include <errno.h>

#include "fnFileSMB.h"
#include "../../include/debug.h"


FileHandlerSMB::FileHandlerSMB(struct smb2_context *smb, struct smb2fh *handle)
{
    Debug_println("new FileHandlerSMB");
    _smb = smb;
    _handle = handle;
};


FileHandlerSMB::~FileHandlerSMB()
{
    Debug_println("delete FileHandlerSMB");
    if (_handle != nullptr) close(false);
}


int FileHandlerSMB::close(bool destroy)
{
    Debug_println("FileHandlerSMB::close");
    int result = 0;
    if (_handle != nullptr) 
    {
        result = smb2_close(_smb, _handle);
        _handle = nullptr;
        _smb = nullptr;
    }
    if (destroy) delete this;
    return result;
}


int FileHandlerSMB::seek(long int off, int whence)
{
    Debug_println("FileHandlerSMB::seek");
    uint64_t new_pos;
    if (smb2_lseek(_smb, _handle, off, whence, &new_pos) < 0)
    {
        Debug_printf("%s\n", smb2_get_error(_smb));
        return -1;
    }
    Debug_printf("new pos is %llu\n", new_pos);
    return 0;
}


long int FileHandlerSMB::tell()
{
    Debug_println("FileHandlerSMB::tell");
    uint64_t pos;
    if (smb2_lseek(_smb, _handle, 0, SEEK_CUR, &pos) < 0)
    {
        Debug_printf("%s\n", smb2_get_error(_smb));
        return -1;
    }
    return (long)pos;
}


size_t FileHandlerSMB::read(void *ptr, size_t size, size_t count)
{
    Debug_println("FileHandlerSMB::read");

    size_t bytes_remaining = size * count;
    size_t bytes_read = 0;
    int result;
    while (bytes_remaining > 0)
    {
        result = smb2_read(_smb, _handle, (uint8_t *)ptr, (uint32_t)bytes_remaining);
        if (result < 0)
        {
            if (errno == EAGAIN)
                continue;
            else
            {
                Debug_printf("%s\n", smb2_get_error(_smb));
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


size_t FileHandlerSMB::write(const void *ptr, size_t size, size_t count)
{
    Debug_println("FileHandlerSMB::write");

    size_t bytes_remaining = size * count;
    size_t bytes_written = 0;
    int result;
    while (bytes_remaining > 0)
    {
        result = smb2_write(_smb, _handle, (uint8_t *)ptr, (uint32_t)bytes_remaining);
        if (result < 0)
        {
            if (errno == EAGAIN)
                continue;
            else
            {
                Debug_printf("%s\n", smb2_get_error(_smb));
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


int FileHandlerSMB::flush()
{
    Debug_println("FileHandlerSMB::flush");
    int result;
    if ((result = smb2_fsync(_smb, _handle)) != 0)
    {
        Debug_printf("%s\n", smb2_get_error(_smb));
        return -1;
    }
    return 0;
}
