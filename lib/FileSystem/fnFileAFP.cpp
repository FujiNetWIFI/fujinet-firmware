#include "fnFileAFP.h"

#include <cstdio>
#include <cstring>

FileHandlerAFP::~FileHandlerAFP()
{
    _file.close();
}

success_is_true FileHandlerAFP::open(const std::string &path, const char *mode)
{
    if (mode == nullptr)
        RETURN_ERROR_AS_FALSE();

    bool plus = strchr(mode, '+') != nullptr;
    AfpOpenMode first;

    switch (mode[0])
    {
    case 'r':
        first = plus ? AfpOpenMode::Update : AfpOpenMode::Read;
        break;
    case 'w':
        first = AfpOpenMode::Write;
        break;
    case 'a':
        first = AfpOpenMode::Append;
        break;
    default:
        RETURN_ERROR_AS_FALSE();
    }

    if (_file.open(path, first).is_error())
        RETURN_ERROR_AS_FALSE();

    // Write and Append are write-only, so "w+" and "a+" make or empty the
    // file that way and then reopen it for both.
    if (plus && first != AfpOpenMode::Update)
    {
        uint64_t end = first == AfpOpenMode::Append ? _file.size() : 0;
        _file.close();
        if (_file.open(path, AfpOpenMode::Update).is_error())
            RETURN_ERROR_AS_FALSE();
        _file.seek(end);
    }

    RETURN_SUCCESS_AS_TRUE();
}

int FileHandlerAFP::close(bool destroy)
{
    int result = _file.close().is_success() ? 0 : -1;
    if (destroy)
        delete this;
    return result;
}

int FileHandlerAFP::seek(long int off, int whence)
{
    int64_t pos;

    switch (whence)
    {
    case SEEK_SET:
        pos = off;
        break;
    case SEEK_CUR:
        pos = static_cast<int64_t>(_file.tell()) + off;
        break;
    case SEEK_END:
        pos = static_cast<int64_t>(_file.size()) + off;
        break;
    default:
        return -1;
    }

    if (pos < 0)
        return -1;

    _file.seek(pos);
    return 0;
}

long int FileHandlerAFP::tell()
{
    return static_cast<long int>(_file.tell());
}

size_t FileHandlerAFP::read(void *ptr, size_t size, size_t count)
{
    size_t want = size * count;
    size_t got = 0;

    // AfpFile may return fewer bytes than asked for; only 0 is the end.
    while (got < want)
    {
        int n = _file.read(static_cast<char *>(ptr) + got, want - got);
        if (n <= 0)
            break;
        got += n;
    }

    return size ? got / size : 0;
}

size_t FileHandlerAFP::write(const void *ptr, size_t size, size_t count)
{
    size_t want = size * count;
    size_t put = 0;

    while (put < want)
    {
        int n = _file.write(static_cast<const char *>(ptr) + put, want - put);
        if (n <= 0)
            break;
        put += n;
    }

    return size ? put / size : 0;
}

int FileHandlerAFP::flush()
{
    // Each write has already gone to the server.
    return 0;
}
