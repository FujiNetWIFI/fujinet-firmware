#ifndef _CAS_SOURCE_H_
#define _CAS_SOURCE_H_

#include <stdint.h>
#include <stddef.h>

// Where a CAS image's bytes come from.
class CasSource
{
public:
    virtual ~CasSource() {}

    virtual uint32_t size() = 0;

    virtual size_t read_at(uint32_t offset, uint8_t *buf, size_t len) = 0;
};

#endif // _CAS_SOURCE_H_
