#ifndef FN_FILENFS_H
#define FN_FILENFS_H

#include <stdint.h>
#include <cstddef>
#include <nfsc/libnfs.h>

#include "fnFile.h"


class FileHandlerNFS : public FileHandler
{
protected:
    struct nfs_context *_nfs;
    struct nfsfh *_handle;
public:
    FileHandlerNFS(struct nfs_context *nfs, struct nfsfh *handle);
    virtual ~FileHandlerNFS() override;

    virtual int close(bool destroy=true) override;
    virtual int seek(long int off, int whence) override;
    virtual long int tell() override;
    virtual size_t read(void *ptr, size_t size, size_t count) override;
    virtual size_t write(const void *ptr, size_t size, size_t count) override;
    virtual int flush() override;
};


#endif // FN_FILENFS_H
