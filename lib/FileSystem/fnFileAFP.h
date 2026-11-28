#ifndef FN_FILEAFP_H
#define FN_FILEAFP_H

#include <memory>
#include <string>

#include "fnFile.h"
#include "global_types.h"

#include "afpFile.h"

// One open file on an AFP volume. It shares the session with the file system
// that made it, so it keeps working if that file system is deleted first.
class FileHandlerAFP : public FileHandler
{
public:
    explicit FileHandlerAFP(std::shared_ptr<AfpSession> session)
        : _session(std::move(session)), _file(*_session) {}
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
    std::shared_ptr<AfpSession> _session; // before _file, which refers to it
    AfpFile _file;
};

#endif // FN_FILEAFP_H
