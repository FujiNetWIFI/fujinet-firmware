
#include <errno.h>
#include <string.h>

#include "fnFileMem.h"
#include "../../include/debug.h"


FileHandlerMem::FileHandlerMem() : _buffer(nullptr), _size(0), _filesize(0), _position(0)
{
//    Debug_println("new FileHandlerMem");
};


FileHandlerMem::~FileHandlerMem()
{
//    Debug_println("delete FileHandlerMem");
    free(_buffer);
}


int FileHandlerMem::close(bool destroy)
{
//    Debug_println("FileHandlerMem::close");
    int result = 0;
    if (destroy) delete this;
    return result;
}


int FileHandlerMem::seek(long int off, int whence)
{
//    Debug_println("FileHandlerMem::seek");
    long int new_pos;
    switch (whence)
    {
        case SEEK_SET:
            new_pos = off;
            break;
        case SEEK_END:
            new_pos = _filesize - off;
            break;
        case SEEK_CUR:
            new_pos = _position + off;
            break;
        default:
            Debug_printf("FileHandlerMem::seek - called with invalid whence value: %d\n", whence);
            errno = EINVAL;
            return -1;
    }

    if (new_pos < 0)
    {
        Debug_printf("FileHandlerMem::seek - invalid new position: %ld\n", new_pos);
        errno = EINVAL;
        return -1;
    }

    // grow file if needed
    if (new_pos > _filesize && grow(new_pos) < 0)
        return -1;

    _position = new_pos;
//    Debug_printf("new pos is %lu\n", new_pos);
    return 0;
}


long int FileHandlerMem::tell()
{
//    Debug_println("FileHandlerMem::tell");
    return _position;
}


size_t FileHandlerMem::read(void *ptr, size_t size, size_t count)
{
//    Debug_println("FileHandlerMem::read");

    size_t requested = size * count;
    size_t available = _filesize - _position;
    size_t to_read = available > requested ? requested : available;

    if (to_read)
    {
        memcpy(ptr, (void *)(_buffer + _position), to_read);
        _position += to_read;
    }

    return (size_t)(size * count == to_read ? count : to_read / size);
}


size_t FileHandlerMem::write(const void *ptr, size_t size, size_t count)
{
//    Debug_println("FileHandlerMem::write");

    size_t requested = size * count;
    size_t available = _size - _position;

    if (available < requested)
    {
        if (grow(_position + requested) < 0)
            return 0;
        available = _size - _position;
    }

    size_t to_write = available > requested ? requested : available;
    if (to_write)
    {
        memcpy((void *)(_buffer + _position), ptr, to_write);
        _position += to_write;
        if (_filesize < _position)
            _filesize = _position;
    }

    return (size_t)(size * count == to_write ? count : to_write / size);
}


int FileHandlerMem::flush()
{
//    Debug_println("FileHandlerMem::flush");
    return 0;
}

// set new file size, allocate additional buffer space, if needed
// (smaller than current file size can be set but it does not shrink allocated buffer)
// return 0 on success, -1 on failure
int FileHandlerMem::grow(long filesize)
{
    long bufsize = 1024 * ((filesize + 1023) / 1024);
//    Debug_printf("FileHandlerMem::grow - file size / buffer size: %ld / %ld\n", filesize, bufsize > _size ? bufsize : _size);
    // grow buffer, if needed
    if (bufsize > _size)
    {
        if (_size >= FILEMEM_MAXSIZE)
        {
            Debug_println("FileHandlerMem::grow - failed, max buffer size reached");
            errno = EFBIG;
            return -1;
        }
#ifdef ESP_PLATFORM
        void *new_buf = heap_caps_realloc(_buffer, bufsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void *new_buf = realloc(_buffer, bufsize);
#endif
        if (new_buf == nullptr) 
        {
            Debug_println("FileHandlerMem::grow - failed to reallocate buffer");
            return -1;
        } 
        _buffer = (uint8_t *)new_buf;
        _size = bufsize;
    }
    // set new file size
    _filesize = filesize;
    return 0;
}
