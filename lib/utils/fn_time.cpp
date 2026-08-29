/**
 * fn_time - see fn_time.h. No libc time functions are used here on purpose.
 */

#include "fn_time.h"

#include <cctype>
#include <cstdlib>

namespace fn_time
{

namespace {

// Floor division / modulus, correct for negative numerators (C++ truncates).
int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}

// A DST transition rule can be expressed three ways in a POSIX TZ string.
enum RuleKind { RULE_MWD, RULE_JULIAN, RULE_YDAY };

// The kind is stashed alongside the rule; PosixTz::Rule keeps only the fields
// the common "Mm.w.d" form needs, so the other two encode their day-of-year in
// `mon`/`week` and are distinguished by `dow == 0xFF`.
//
// Rather than widen the public struct, encode as: mon==0 -> RULE_JULIAN (yday in
// week), mon==13 -> RULE_YDAY (yday in week). Anything else is RULE_MWD.
RuleKind rule_kind(const PosixTz::Rule &r)
{
    if (r.mon == 0)  return RULE_JULIAN;
    if (r.mon == 13) return RULE_YDAY;
    return RULE_MWD;
}

// Day number (days since epoch) on which a transition rule fires in year y.
int64_t rule_day(const PosixTz::Rule &r, int y)
{
    switch (rule_kind(r))
    {
    case RULE_JULIAN: // Jn: 1..365, February 29 is never counted
    {
        int64_t d = days_from_civil(y, 1, 1) + (int64_t)r.week - 1;
        if (is_leap(y) && r.week > 59) d += 1;
        return d;
    }
    case RULE_YDAY: // n: 0..365, February 29 is counted
        return days_from_civil(y, 1, 1) + (int64_t)r.week;

    case RULE_MWD:
    default:
        if (r.week >= 5) // "last <dow> of the month"
        {
            int64_t last = days_from_civil(y, r.mon, days_in_month(y, r.mon));
            int back = (weekday_from_days(last) - (int)r.dow + 7) % 7;
            return last - back;
        }
        else
        {
            int64_t first = days_from_civil(y, r.mon, 1);
            int fwd = ((int)r.dow - weekday_from_days(first) + 7) % 7;
            return first + fwd + (int64_t)(r.week - 1) * 7;
        }
    }
}

// A zone abbreviation: either a run of letters, or a <...> quoted form.
bool parse_name(const std::string &s, size_t &i, std::string &out)
{
    out.clear();
    if (i < s.size() && s[i] == '<')
    {
        size_t e = s.find('>', i);
        if (e == std::string::npos) return false;
        out = s.substr(i + 1, e - i - 1);
        i = e + 1;
        return !out.empty();
    }
    while (i < s.size() && isalpha((unsigned char)s[i]))
        out += s[i++];
    return !out.empty();
}

// [+|-]hh[:mm[:ss]]. POSIX writes the offset to ADD to local time to reach UTC,
// so the sign is flipped here to store seconds EAST of UTC.
bool parse_offset(const std::string &s, size_t &i, int &out)
{
    int sign = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
    {
        if (s[i] == '-') sign = -1;
        i++;
    }
    if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;

    int h = 0, m = 0, sec = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) h = h * 10 + (s[i++] - '0');
    if (i < s.size() && s[i] == ':')
    {
        i++;
        while (i < s.size() && isdigit((unsigned char)s[i])) m = m * 10 + (s[i++] - '0');
        if (i < s.size() && s[i] == ':')
        {
            i++;
            while (i < s.size() && isdigit((unsigned char)s[i])) sec = sec * 10 + (s[i++] - '0');
        }
    }
    out = -(sign * (h * 3600 + m * 60 + sec));
    return true;
}

// "M3.2.0[/2]", "J60[/2]" or "60[/2]".
bool parse_rule(const std::string &s, size_t &i, PosixTz::Rule &r)
{
    if (i >= s.size()) return false;

    if (s[i] == 'M')
    {
        i++;
        long mon = strtol(s.c_str() + i, nullptr, 10);
        while (i < s.size() && isdigit((unsigned char)s[i])) i++;
        if (i >= s.size() || s[i] != '.') return false;
        i++;
        long week = strtol(s.c_str() + i, nullptr, 10);
        while (i < s.size() && isdigit((unsigned char)s[i])) i++;
        if (i >= s.size() || s[i] != '.') return false;
        i++;
        long dow = strtol(s.c_str() + i, nullptr, 10);
        while (i < s.size() && isdigit((unsigned char)s[i])) i++;
        if (mon < 1 || mon > 12 || week < 1 || week > 5 || dow < 0 || dow > 6) return false;
        r.mon = (unsigned)mon;
        r.week = (unsigned)week;
        r.dow = (unsigned)dow;
    }
    else if (s[i] == 'J' || isdigit((unsigned char)s[i]))
    {
        bool julian = (s[i] == 'J');
        if (julian) i++;
        if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;
        long n = strtol(s.c_str() + i, nullptr, 10);
        while (i < s.size() && isdigit((unsigned char)s[i])) i++;
        if (n < 0 || n > 365) return false;
        r.mon = julian ? 0 : 13; // see rule_kind()
        r.week = (unsigned)n;
        r.dow = 0;
    }
    else
        return false;

    r.sec = 7200; // POSIX default transition time is 02:00:00 local
    if (i < s.size() && s[i] == '/')
    {
        i++;
        int off = 0;
        if (!parse_offset(s, i, off)) return false;
        r.sec = -off; // parse_offset negates; transition times are not offsets
    }
    return true;
}

} // namespace

// ─── civil calendar ───────────────────────────────────────────────────────────

bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

unsigned days_in_month(int y, unsigned m)
{
    static const unsigned d[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 30;
    if (m == 2 && is_leap(y)) return 29;
    return d[m];
}

int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t  era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                          // [0,399]
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;  // [0,365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;              // [0,146096]
    return era * 146097LL + (int64_t)doe - 719468;
}

void civil_from_days(int64_t z, int &y, unsigned &m, unsigned &d)
{
    z += 719468;
    const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);                          // [0,146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0,399]
    const int64_t  yy  = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               // [0,365]
    const unsigned mp  = (5 * doy + 2) / 153;                                   // [0,11]
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = (int)(yy + (m <= 2));
}

int weekday_from_days(int64_t z)
{
    return (int)((z % 7 + 11) % 7); // 1970-01-01 was a Thursday
}

int64_t fn_timegm(int y, unsigned mo, unsigned d, int h, int mi, int s)
{
    return days_from_civil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + s;
}

// ─── PosixTz ──────────────────────────────────────────────────────────────────

bool PosixTz::parse(const std::string &tz)
{
    *this = PosixTz();

    if (tz.empty()) return false;

    // ":Area/City" and bare IANA names cannot be resolved without a tz database;
    // treat them as UTC rather than guessing. Only the part before the rules is
    // checked - a rule may legitimately carry a "/hh" transition time.
    if (tz[0] == ':') return false;
    if (tz.substr(0, tz.find(',')).find('/') != std::string::npos) return false;

    size_t i = 0;
    if (!parse_name(tz, i, stdName)) return false;
    if (!parse_offset(tz, i, stdOff))
    {
        // "UTC" with no offset is legal and means UTC.
        stdOff = 0;
        return i >= tz.size();
    }

    if (i >= tz.size()) return true; // no DST part

    if (tz[i] != ',')
    {
        if (!parse_name(tz, i, dstName)) return false;
        hasDst = true;
        size_t save = i;
        if (!parse_offset(tz, i, dstOff))
        {
            i = save;
            dstOff = stdOff + 3600; // POSIX default: one hour ahead of standard
        }
    }

    if (i < tz.size() && tz[i] == ',')
    {
        i++;
        if (!parse_rule(tz, i, start)) return false;
        if (i >= tz.size() || tz[i] != ',') return false;
        i++;
        if (!parse_rule(tz, i, end)) return false;
        hasDst = true;
        if (dstName.empty()) dstOff = stdOff + 3600;
    }
    else if (hasDst)
    {
        // A DST abbreviation with no rules: assume the US rules, matching what
        // glibc and newlib do. The web UI's "PST+8PDT"-style presets rely on it.
        start.mon = 3;  start.week = 2; start.dow = 0; start.sec = 7200;
        end.mon   = 11; end.week   = 1; end.dow   = 0; end.sec   = 7200;
    }

    return true;
}

int PosixTz::offset_at(int64_t utc) const
{
    if (!hasDst) return stdOff;

    int y;
    unsigned mo, d;
    civil_from_days(floor_div(utc + stdOff, 86400), y, mo, d);

    // Transition instants, converted to UTC using the offset in effect just
    // before each one.
    int64_t into = rule_day(start, y) * 86400LL + start.sec - stdOff;
    int64_t outof = rule_day(end, y) * 86400LL + end.sec - dstOff;

    if (into <= outof)
        return (utc >= into && utc < outof) ? dstOff : stdOff; // northern hemisphere
    return (utc >= into || utc < outof) ? dstOff : stdOff;     // southern hemisphere
}

int64_t PosixTz::from_local(int y, unsigned mo, unsigned d, int h, int mi, int s) const
{
    const int64_t naive = fn_timegm(y, mo, d, h, mi, s);
    if (!hasDst) return naive - stdOff;

    const int64_t c1 = naive - stdOff;
    const int64_t c2 = naive - dstOff;
    const bool ok1 = (offset_at(c1) == stdOff);
    const bool ok2 = (offset_at(c2) == dstOff);

    if (ok1 && ok2) return c1 < c2 ? c1 : c2; // fall-back overlap: earlier instant
    if (ok1) return c1;
    if (ok2) return c2;
    return c1 > c2 ? c1 : c2; // spring-forward gap: first instant that exists
}

int64_t PosixTz::from_local_days(int64_t days, int h, int mi, int s) const
{
    int y;
    unsigned mo, d;
    civil_from_days(days, y, mo, d);
    return from_local(y, mo, d, h, mi, s);
}

void PosixTz::to_local(int64_t utc, int &y, unsigned &mo, unsigned &d,
                       int &h, int &mi, int &s, int &wd) const
{
    const int64_t local = utc + offset_at(utc);
    const int64_t days = floor_div(local, 86400);
    const int64_t rem = local - days * 86400;
    civil_from_days(days, y, mo, d);
    h = (int)(rem / 3600);
    mi = (int)((rem % 3600) / 60);
    s = (int)(rem % 60);
    wd = weekday_from_days(days);
}

int64_t PosixTz::local_day(int64_t utc) const
{
    return floor_div(utc + offset_at(utc), 86400);
}

// ─── parsing ──────────────────────────────────────────────────────────────────

namespace {

// Read exactly n digits into v. Returns false if fewer are available.
bool take_digits(const std::string &s, size_t &i, int n, int &v)
{
    v = 0;
    for (int k = 0; k < n; k++)
    {
        if (i >= s.size() || !isdigit((unsigned char)s[i])) return false;
        v = v * 10 + (s[i++] - '0');
    }
    return true;
}

} // namespace

bool parse_datetime(const std::string &in, ParsedTime &out)
{
    out = ParsedTime();

    // Trim surrounding whitespace.
    size_t b = in.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = in.find_last_not_of(" \t\r\n");
    const std::string s = in.substr(b, e - b + 1);

    size_t i = 0;
    int v;

    if (!take_digits(s, i, 4, v)) return false;
    out.year = v;
    const bool dashed = (i < s.size() && s[i] == '-');
    if (dashed) i++;
    if (!take_digits(s, i, 2, v)) return false;
    out.month = (unsigned)v;
    if (dashed)
    {
        if (i >= s.size() || s[i] != '-') return false;
        i++;
    }
    if (!take_digits(s, i, 2, v)) return false;
    out.day = (unsigned)v;

    if (out.month < 1 || out.month > 12 || out.day < 1 || out.day > 31) return false;

    if (i >= s.size())
    {
        out.dateOnly = true;
        return true;
    }

    if (s[i] != 'T' && s[i] != 't' && s[i] != ' ') return false;
    i++;

    if (!take_digits(s, i, 2, v)) return false;
    out.hour = v;
    const bool colons = (i < s.size() && s[i] == ':');
    if (colons) i++;
    if (!take_digits(s, i, 2, v)) return false;
    out.minute = v;
    if (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == ':'))
    {
        if (colons)
        {
            if (s[i] != ':') return false;
            i++;
        }
        if (!take_digits(s, i, 2, v)) return false;
        out.second = v;
    }

    // Fractional seconds are accepted and discarded.
    if (i < s.size() && s[i] == '.')
    {
        i++;
        while (i < s.size() && isdigit((unsigned char)s[i])) i++;
    }

    if (i >= s.size()) return true;

    if (s[i] == 'Z' || s[i] == 'z')
    {
        out.utc = true;
        return i + 1 == s.size();
    }

    if (s[i] == '+' || s[i] == '-')
    {
        const int sign = (s[i] == '-') ? -1 : 1;
        i++;
        int oh = 0, om = 0;
        if (!take_digits(s, i, 2, oh)) return false;
        if (i < s.size() && s[i] == ':') i++;
        if (i < s.size() && !take_digits(s, i, 2, om)) return false;
        out.hasOffset = true;
        out.offset = sign * (oh * 3600 + om * 60);
        return i == s.size();
    }

    return false;
}

bool parse_yearmonth(const std::string &in, ParsedTime &out)
{
    out = ParsedTime();

    size_t b = in.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = in.find_last_not_of(" \t\r\n");
    const std::string s = in.substr(b, e - b + 1);

    size_t i = 0;
    int v;
    if (!take_digits(s, i, 4, v)) return false;
    out.year = v;
    if (i < s.size() && s[i] == '-') i++;
    if (!take_digits(s, i, 2, v)) return false;
    out.month = (unsigned)v;
    if (i != s.size()) return false;
    if (out.month < 1 || out.month > 12) return false;

    out.day = 1;
    out.dateOnly = true;
    return true;
}

int64_t resolve(const ParsedTime &p, const PosixTz &tz)
{
    if (p.utc)
        return fn_timegm(p.year, p.month, p.day, p.hour, p.minute, p.second);
    if (p.hasOffset)
        return fn_timegm(p.year, p.month, p.day, p.hour, p.minute, p.second) - p.offset;
    return tz.from_local(p.year, p.month, p.day, p.hour, p.minute, p.second);
}

} // namespace fn_time
