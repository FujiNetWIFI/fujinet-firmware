/**
 * Column and wire-field helpers shared by the protocol adapters that render
 * human-readable listings for a retro host (Mailbox, Calendar).
 *
 * These were originally file-local to Mailbox.cpp; they are hoisted here so the
 * calendar adapters format identically rather than carrying a second copy.
 */

#ifndef NETWORKPROTOCOL_TEXT_FORMAT_H
#define NETWORKPROTOCOL_TEXT_FORMAT_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace fnfmt
{

// Truncate to w columns, marking the cut with an ellipsis when there is room.
inline std::string ellipsize(const std::string &s, int w)
{
    if (w <= 0) return "";
    if ((int)s.size() <= w) return s;
    if (w <= 3) return s.substr(0, w);
    return s.substr(0, w - 3) + "...";
}

// Pad right / truncate to exactly w columns.
inline std::string ljust(const std::string &s, int w)
{
    if ((int)s.size() >= w) return s.substr(0, w);
    return s + std::string(w - s.size(), ' ');
}

// Pad left / truncate to exactly w columns.
inline std::string rjust(const std::string &s, int w)
{
    if ((int)s.size() >= w) return s.substr(0, w);
    return std::string(w - s.size(), ' ') + s;
}

// Append `n` little-endian bytes of `v`.
inline void append_le(std::string &b, uint64_t v, int n)
{
    for (int i = 0; i < n; i++)
    {
        b += (char)(v & 0xFF);
        v >>= 8;
    }
}

// Append a fixed-width, NUL-terminated, NUL-padded char field.
inline void append_fixed(std::string &b, const std::string &s, size_t n)
{
    size_t c = (n > 0) ? std::min(s.size(), n - 1) : 0;
    b.append(s.data(), c);
    b.append(n - c, '\0');
}

// "31 Jul" within the current year, "07/31/23" otherwise. Uses the process
// timezone; callers that need a specific zone should format it themselves.
inline std::string humanize_date(uint64_t ts)
{
    time_t t = (time_t)ts;
    time_t now = time(nullptr);
    struct tm tmv;
    struct tm nowv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
    localtime_s(&nowv, &now);
#else
    localtime_r(&t, &tmv);
    localtime_r(&now, &nowv);
#endif

    char buf[16];
    if (tmv.tm_year == nowv.tm_year)
        strftime(buf, sizeof(buf), "%d %b", &tmv);
    else
        strftime(buf, sizeof(buf), "%m/%d/%y", &tmv);
    return buf;
}

inline std::string humanize_size(uint64_t n)
{
    char buf[16];
    if (n < 1024ULL)
        snprintf(buf, sizeof(buf), "%uB", (unsigned)n);
    else if (n < 1024ULL * 1024)
        snprintf(buf, sizeof(buf), "%.1fK", n / 1024.0);
    else if (n < 1024ULL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1fM", n / (1024.0 * 1024));
    else
        snprintf(buf, sizeof(buf), "%.1fG", n / (1024.0 * 1024 * 1024));
    return buf;
}

} // namespace fnfmt

#endif /* NETWORKPROTOCOL_TEXT_FORMAT_H */
