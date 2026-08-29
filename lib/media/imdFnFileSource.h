#ifndef IMD_FNFILE_SOURCE_H
#define IMD_FNFILE_SOURCE_H

// fnFile-backed ImdSource, for platform adapters.
//
// Split out of imdImage.h so the core stays free of fnio: fnFile is FileHandler
// on some targets and std::FILE on others, and dragging that into the core would
// make every caller target-specific.

#include "fnio.h"
#include "imdImage.h"

class ImdFnFileSource : public ImdSource
{
public:
    ImdFnFileSource(fnFile *f, bool writable) : _f(f), _writable(writable)
    {
        if (_f != nullptr && fnio::fseek(_f, 0, SEEK_END) == 0)
        {
            long end = fnio::ftell(_f);
            if (end > 0)
                _size = (uint32_t)end;
        }
    }

    bool read_at(uint32_t off, void *dst, uint32_t len) override
    {
        if (_f == nullptr)
            return false;
        if (len == 0)
            return true;
        if (off > _size || len > _size - off)
            return false;
        if (fnio::fseek(_f, (long)off, SEEK_SET) != 0)
            return false;
        return fnio::fread(dst, 1, len, _f) == len;
    }

    bool write_at(uint32_t off, const void *src, uint32_t len) override
    {
        if (_f == nullptr || !_writable)
            return false;
        if (len == 0)
            return true;
        if (off > _size || len > _size - off)
            return false;
        if (fnio::fseek(_f, (long)off, SEEK_SET) != 0)
            return false;
        return fnio::fwrite(src, 1, len, _f) == len;
    }

    uint32_t size() override { return _size; }

    // fnio::fflush also fsyncs on ESP, where we can be reset at any moment
    bool flush() override { return _f != nullptr && fnio::fflush(_f) == 0; }

    bool writable() const override { return _writable; }

private:
    fnFile  *_f = nullptr;
    bool     _writable = false;
    uint32_t _size = 0;
};

#endif // IMD_FNFILE_SOURCE_H
