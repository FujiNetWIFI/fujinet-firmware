#ifndef _CAS_SOURCE_FILE_H_
#define _CAS_SOURCE_FILE_H_

#include "casSource.h"
#include "mediaType.h"

// CasSource backed by a mounted fnFile. Image contents are deliberately not
// cached here; CasReader's window is what bounds the read count.
class CasSourceFile : public CasSource
{
public:
    void attach(fnFile *f, uint32_t size)
    {
        _f = f;
        _size = size;
        _pos = INVALID_SECTOR_VALUE;
    }

    uint32_t size() override { return _size; }

    size_t read_at(uint32_t offset, uint8_t *buf, size_t len) override
    {
        if (_f == nullptr || offset >= _size)
            return 0;
        if (offset + len > _size)
            len = _size - offset;

        if (_pos != offset)
        {
            if (fnio::fseek(_f, (long)offset, SEEK_SET) != 0)
            {
                _pos = INVALID_SECTOR_VALUE;
                return 0;
            }
        }

        size_t got = fnio::fread(buf, 1, len, _f);
        _pos = (got == len) ? offset + (uint32_t)got : INVALID_SECTOR_VALUE;
        return got;
    }

private:
    fnFile  *_f = nullptr;
    uint32_t _size = 0;
    uint32_t _pos = INVALID_SECTOR_VALUE;
};

#endif // _CAS_SOURCE_FILE_H_
