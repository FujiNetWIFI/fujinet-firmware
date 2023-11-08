#ifndef ESP_PLATFORM

#include <unistd.h>  // for fsync
#include "fnFileLocal.h"

#include "../../include/debug.h"


FileHandlerLocal::FileHandlerLocal(FILE *fh)
{
    Debug_println("new FileHandlerLocal");
    _fh = fh;
};


FileHandlerLocal::~FileHandlerLocal()
{
    Debug_println("delete FileHandlerLocal");
    if (_fh != nullptr) close(false);
}


int FileHandlerLocal::close(bool destroy)
{
    Debug_println("FileHandlerLocal::close");
    int result = 0;
    if (_fh != nullptr) 
    {
        result = fclose(_fh);
        _fh = nullptr;
    }
    if (destroy) delete this;
    return result;
}


int FileHandlerLocal::seek(long int off, int whence)
{
    Debug_println("FileHandlerLocal::seek");
    return fseek(_fh, off, whence);
}


long int FileHandlerLocal::tell()
{
    Debug_println("FileHandlerLocal::tell");
    return ftell(_fh);
}


size_t FileHandlerLocal::read(void *ptr, size_t size, size_t n)
{
    Debug_println("FileHandlerLocal::read");
    return fread(ptr, size, n, _fh);
}


size_t FileHandlerLocal::write(const void *ptr, size_t size, size_t n)
{
    Debug_println("FileHandlerLocal::write");
    return fwrite(ptr, size, n, _fh);
}


int FileHandlerLocal::flush()
{
    Debug_println("FileHandlerLocal::flush");
    int ret = fflush(_fh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    // ret = fsync(fileno(_fh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    return ret;
}

#endif // !ESP_PLATFORM