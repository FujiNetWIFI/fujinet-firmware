#ifndef FN_FILELOCAL_H
#define FN_FILELOCAL_H

#include <cstdio>
#include "fnFile.h"


class FileHandlerLocal : public FileHandler
{
protected:
    FILE *_fh = nullptr;

public:
    FileHandlerLocal(FILE *fh);
    virtual ~FileHandlerLocal() override;

    virtual int close(bool destroy=true) override;
    virtual int seek(long int off, int whence) override;
    virtual long int tell() override;
    virtual size_t read(void *ptr, size_t size, size_t n) override;
    virtual size_t write(const void *ptr, size_t size, size_t n) override;
    virtual int flush() override;
};


#endif // FN_FILELOCAL_H
