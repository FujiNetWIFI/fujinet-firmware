#ifndef IMD_FIXTURE_H
#define IMD_FIXTURE_H

// Shared IMD image builder for the IMDImage and MediaTypeIMD test targets.
// Follows IMD.TXT section 6 field order.

#include <cstdint>
#include <vector>

#include "IMDImage.h"

struct IMD
{
    std::vector<uint8_t> out;

    IMD &header(const char *sig = "IMD 1.18: 01/01/2024 12:00:00\r\n",
                const char *comment = "test\r\n")
    {
        if (sig)
            for (const char *p = sig; *p; p++)
                out.push_back((uint8_t)*p);
        if (comment)
            for (const char *p = comment; *p; p++)
                out.push_back((uint8_t)*p);
        out.push_back(IMD_EOF_MARKER);
        return *this;
    }

    IMD &thdr(uint8_t mode, uint8_t cyl, uint8_t head_raw, uint8_t nsec, uint8_t size_code)
    {
        out.push_back(mode);
        out.push_back(cyl);
        out.push_back(head_raw);
        out.push_back(nsec);
        out.push_back(size_code);
        return *this;
    }

    IMD &map(const std::vector<uint8_t> &m)
    {
        for (uint8_t b : m)
            out.push_back(b);
        return *this;
    }

    IMD &sizes(const std::vector<uint16_t> &s)
    {
        for (uint16_t v : s)
        {
            out.push_back((uint8_t)(v & 0xFF));
            out.push_back((uint8_t)(v >> 8));
        }
        return *this;
    }

    IMD &unavailable()
    {
        out.push_back(0x00);
        return *this;
    }

    IMD &full(uint8_t type, uint16_t size, uint8_t fill)
    {
        out.push_back(type);
        for (uint16_t i = 0; i < size; i++)
            out.push_back(fill);
        return *this;
    }

    IMD &comp(uint8_t type, uint8_t fill)
    {
        out.push_back(type);
        out.push_back(fill);
        return *this;
    }

    IMD &raw(uint8_t b)
    {
        out.push_back(b);
        return *this;
    }

    IMD &pad(uint32_t n, uint8_t v)
    {
        for (uint32_t i = 0; i < n; i++)
            out.push_back(v);
        return *this;
    }

    // Sequential ids 1..nsec, size code 0 (128B), every sector compressed
    IMD &simple_track(uint8_t mode, uint8_t cyl, uint8_t head_raw, uint8_t nsec, uint8_t fill)
    {
        thdr(mode, cyl, head_raw, nsec, 0);
        std::vector<uint8_t> ids;
        for (uint8_t i = 1; i <= nsec; i++)
            ids.push_back(i);
        map(ids);
        for (uint8_t i = 0; i < nsec; i++)
            comp(0x02, fill);
        return *this;
    }
};

#endif // IMD_FIXTURE_H
