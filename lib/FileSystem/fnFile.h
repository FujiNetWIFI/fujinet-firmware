#ifndef FN_FILE_H
#define FN_FILE_H

#include <cstdio>

#ifdef ESP_PLATFORM

typedef std::FILE fnFile;

#else

/* 
FileHandler - stdlib's FILE abstraction to allow implement other file protocols in application (no need for kernel/FUSE drivers)
*/

// TODO rename FileHandler to fnFile
class FileHandler
{

public:
    virtual ~FileHandler() = 0;

    virtual int close(bool destroy=true) = 0;
    virtual int seek(long int off, int whence) = 0;
    virtual long int tell() = 0;
    virtual size_t read(void *ptr, size_t size, size_t n) = 0;
    virtual size_t write(const void *ptr, size_t size, size_t n) = 0;
    virtual int flush() = 0;
    virtual int eof() {return 0;}; // TODO!
};

typedef FileHandler fnFile;

#endif

#endif // FN_FILE_H
