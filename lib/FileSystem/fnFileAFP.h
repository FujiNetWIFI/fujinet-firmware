#ifndef FN_FILEAFP_H
#define FN_FILEAFP_H

#include <string>

#include "fnFile.h"
#include "global_types.h"

#include "afpFile.h"

// One open file on an AFP volume. It borrows the session, so the file system
// that made it must outlive it.
class FileHandlerAFP : public FileHandler
{
public:
    explicit FileHandlerAFP(AfpSession &session) : _file(session) {}
    ~FileHandlerAFP() override;

    // path is relative to the volume and starts with '/'; mode is as fopen's.
    success_is_true open(const std::string &path, const char *mode);

    int close(bool destroy = true) override;
    int seek(long int off, int whence) override;
    long int tell() override;
    size_t read(void *ptr, size_t size, size_t count) override;
    size_t write(const void *ptr, size_t size, size_t count) override;
    int flush() override;

private:
    AfpFile _file;
};

#endif // FN_FILEAFP_H
