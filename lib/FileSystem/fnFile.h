#ifndef FN_FILE_H
#define FN_FILE_H

#include <cstddef>

/* 
 * FileHandler - abstraction of FILE from stdio
 * it allows to implement other file protocols at application layer
 * no need to use kernel/VFS or FUSE drivers
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

#endif // FN_FILE_H
