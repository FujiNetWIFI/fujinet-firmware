#ifndef IMD_FNIO_STREAM_H
#define IMD_FNIO_STREAM_H

#include "imd/IMDImage.h"

#include "fnio.h"

// An IMDStream over an fnFile, so IMDImage works the same on every host
// FujiNet mounts from. Borrows the handle and never closes it.
class IMDFnioStream : public IMDStream
{
public:
    IMDFnioStream() = default;
    IMDFnioStream(fnFile *f, uint32_t size) : _f(f), _size(size) {}

    uint32_t size() const override { return _size; }

    IMDStatus read_at(uint32_t off, void *dst, uint32_t len) override
    {
        if (fnio::fseek(_f, (long)off, SEEK_SET) != 0 || fnio::fread(dst, 1, len, _f) != len)
            return IMDStatus::IoError;
        return IMDStatus::Ok;
    }

    IMDStatus write_at(uint32_t off, const void *src, uint32_t len) override
    {
        if (fnio::fseek(_f, (long)off, SEEK_SET) != 0 || fnio::fwrite(src, 1, len, _f) != len)
            return IMDStatus::IoError;
        if (off + len > _size)
            _size = off + len;
        return IMDStatus::Ok;
    }

    IMDStatus flush() override
    {
        fnio::fflush(_f);
        return IMDStatus::Ok;
    }

private:
    fnFile  *_f = nullptr;
    uint32_t _size = 0;
};

#endif // IMD_FNIO_STREAM_H
