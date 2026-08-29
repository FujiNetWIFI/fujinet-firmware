/**
 * fn_time - civil date arithmetic, a timegm shim, and a self-contained POSIX TZ
 * evaluator.
 *
 * The C library cannot be used for this. `timegm` is absent on Windows, and
 * MSVC's `_tzset` does not implement the ",M<m>.<w>.<d>" DST rule syntax at all
 * (it silently applies US transition dates to every zone), so the POSIX TZ
 * strings stored by fnConfig would be misinterpreted on the FujiNet-PC build.
 * Everything here is pure computation: no global state, no tzset(), reentrant,
 * and identical on ESP32, Linux and Windows.
 */

#ifndef FN_TIME_H
#define FN_TIME_H

#include <cstdint>
#include <string>

namespace fn_time
{

// ─── civil calendar (Howard Hinnant's algorithms; exact over the int64 range) ──

// Days since 1970-01-01 for a proleptic Gregorian date. m is [1,12], d is [1,31].
int64_t days_from_civil(int y, unsigned m, unsigned d);

// Inverse of days_from_civil.
void civil_from_days(int64_t z, int &y, unsigned &m, unsigned &d);

// Day of week for a day number, 0=Sunday. 1970-01-01 was a Thursday.
int weekday_from_days(int64_t z);

// Days in month m of year y.
unsigned days_in_month(int y, unsigned m);

bool is_leap(int y);

// ─── the missing timegm ───────────────────────────────────────────────────────

// UTC epoch seconds for a broken-down UTC time. Accepts out-of-range h/mi/s.
int64_t fn_timegm(int y, unsigned mo, unsigned d, int h, int mi, int s);

// ─── POSIX TZ ─────────────────────────────────────────────────────────────────

/**
 * A parsed POSIX TZ string, e.g. "CST+6CDT,M3.2.0/2,M11.1.0/2" or "CET-1CEST".
 *
 * Offsets are stored seconds EAST of UTC, i.e. the negation of the POSIX field
 * (POSIX writes the offset you ADD to local time to get UTC).
 *
 * When a DST abbreviation is present but no rules follow, the US rules are
 * assumed - the same fallback glibc and newlib use, and what the web UI's
 * rule-less American presets ("PST+8PDT") rely on.
 */
struct PosixTz
{
    struct Rule
    {
        unsigned mon  = 3;    // 1-12
        unsigned week = 2;    // 1-5, where 5 means "last"
        unsigned dow  = 0;    // 0=Sunday
        int      sec  = 7200; // seconds after local midnight of the transition day
    };

    int  stdOff = 0;     // seconds east of UTC
    int  dstOff = 0;
    bool hasDst = false;
    Rule start;          // into DST
    Rule end;            // out of DST
    std::string stdName = "UTC";
    std::string dstName;

    // Parse a POSIX TZ string. Returns false and leaves *this as plain UTC when
    // the string cannot be understood.
    bool parse(const std::string &tz);

    // Offset in effect at a UTC instant.
    int offset_at(int64_t utc) const;

    // UTC instant for a local wall-clock time, reproducing mktime(tm_isdst=-1):
    // a fall-back overlap resolves to the earlier (still-DST) instant, and a
    // spring-forward gap resolves to the first instant that actually exists.
    int64_t from_local(int y, unsigned mo, unsigned d, int h, int mi, int s) const;

    // Convenience: local midnight-relative time on a given day number.
    int64_t from_local_days(int64_t days, int h, int mi, int s) const;

    // Broken-down local time for a UTC instant. wd is 0=Sunday.
    void to_local(int64_t utc, int &y, unsigned &mo, unsigned &d,
                  int &h, int &mi, int &s, int &wd) const;

    // Day number (days since epoch) of the local date containing a UTC instant.
    int64_t local_day(int64_t utc) const;
};

// ─── parsing ──────────────────────────────────────────────────────────────────

// Result of parsing a date or date-time.
struct ParsedTime
{
    int      year = 1970;
    unsigned month = 1, day = 1;
    int      hour = 0, minute = 0, second = 0;
    bool     dateOnly = false; // "YYYY-MM-DD" / "YYYYMMDD" with no time part
    bool     utc = false;      // trailing 'Z'
    bool     hasOffset = false;
    int      offset = 0;       // seconds east of UTC, when hasOffset
};

/**
 * Parse an ISO 8601 / RFC 3339 / iCalendar date or date-time. Accepts:
 *   YYYY-MM-DD              YYYYMMDD
 *   YYYY-MM-DDTHH:MM[:SS]   YYYYMMDDTHHMMSS
 * with an optional trailing 'Z' or "+HH:MM" / "-HHMM" offset. A space may stand
 * in for the 'T'. Fractional seconds are accepted and discarded.
 */
bool parse_datetime(const std::string &s, ParsedTime &out);

// "YYYY-MM" or "YYYYMM". Sets day = 1 and dateOnly.
bool parse_yearmonth(const std::string &s, ParsedTime &out);

/**
 * Resolve a ParsedTime to UTC epoch seconds. A value carrying 'Z' or an explicit
 * offset is absolute; anything else is floating local time and is resolved
 * through `tz`.
 */
int64_t resolve(const ParsedTime &p, const PosixTz &tz);

} // namespace fn_time

#endif /* FN_TIME_H */
