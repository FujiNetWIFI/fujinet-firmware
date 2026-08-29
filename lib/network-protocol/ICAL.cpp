/**
 * NetworkProtocolICAL
 *
 * Streams an iCalendar (RFC 5545) feed over http/https and renders it through
 * the NetworkProtocolCalendar template. Both HTTP client implementations expose
 * the same begin/GET/read surface, so there is one code path for ESP32 and PC.
 */

#include "ICAL.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "../../include/debug.h"
#include "../utils/string_utils.h"
#include "status_error_codes.h"

using namespace fn_time;

// Matches the HTTP clients' own internal buffer size.
#define ICS_CHUNK 512
// Logical line cap after unfolding. Longer lines are clipped, not dropped, so
// that a following continuation is not mis-attributed to the next property.
#define ICS_MAX_LINE 1024
// Backstop on recurrence expansion, so a malformed rule cannot spin forever.
#define ICS_MAX_ITER 20000
// Upper bound on RECURRENCE-ID overrides retained for the end-of-feed fixup.
#define ICS_MAX_OVERRIDES 256

namespace {

// ─── line-level helpers ───────────────────────────────────────────────────────

// Index of the first ':' that is not inside a quoted parameter value.
// DTSTART;TZID="America/New_York":2026... is legal, so a plain find(':') is wrong.
size_t split_at_colon(const std::string &line)
{
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++)
    {
        if (line[i] == '"') quoted = !quoted;
        else if (line[i] == ':' && !quoted) return i;
    }
    return std::string::npos;
}

struct IcsProp
{
    std::string name;
    std::string params;
    std::string value;
};

bool parse_prop(const std::string &line, IcsProp &p)
{
    size_t c = split_at_colon(line);
    if (c == std::string::npos) return false;

    const std::string head = line.substr(0, c);
    p.value = line.substr(c + 1);

    size_t sc = head.find(';');
    if (sc == std::string::npos)
    {
        p.name = head;
        p.params.clear();
    }
    else
    {
        p.name = head.substr(0, sc);
        p.params = head.substr(sc + 1);
    }
    mstr::toUpper(p.name);
    return true;
}

// Look up one parameter in a property's parameter list.
std::string param_value(const std::string &params, const char *key)
{
    size_t i = 0;
    while (i < params.size())
    {
        bool quoted = false;
        size_t j = i;
        for (; j < params.size(); j++)
        {
            if (params[j] == '"') quoted = !quoted;
            else if (params[j] == ';' && !quoted) break;
        }
        std::string kv = params.substr(i, j - i);
        size_t eq = kv.find('=');
        if (eq != std::string::npos)
        {
            std::string k = kv.substr(0, eq);
            mstr::toUpper(k);
            if (k == key)
            {
                std::string v = kv.substr(eq + 1);
                if (v.size() >= 2 && v[0] == '"' && v[v.size() - 1] == '"')
                    v = v.substr(1, v.size() - 2);
                return v;
            }
        }
        i = j + 1;
    }
    return "";
}

// RFC 5545 text escaping: \n \, \; \\ .
std::string ics_unescape(const std::string &s)
{
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            char c = s[++i];
            switch (c)
            {
            case 'n':
            case 'N': o += '\n'; break;
            case ',': o += ','; break;
            case ';': o += ';'; break;
            case '\\': o += '\\'; break;
            default: o += c; break;
            }
        }
        else
            o += s[i];
    }
    return o;
}

// Split a comma-separated list on UNESCAPED commas, then unescape each item.
std::vector<std::string> ics_split_list(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            cur += s[i];
            cur += s[i + 1];
            i++;
        }
        else if (s[i] == ',')
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur += s[i];
    }
    out.push_back(cur);
    for (auto &x : out)
    {
        x = ics_unescape(x);
        mstr::trim(x);
    }
    return out;
}

// "PT1H30M", "P1D", "-PT15M".
bool ics_duration(const std::string &s, int64_t &secs)
{
    size_t i = 0;
    int64_t sign = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
    {
        if (s[i] == '-') sign = -1;
        i++;
    }
    if (i >= s.size() || (s[i] != 'P' && s[i] != 'p')) return false;
    i++;

    int64_t total = 0;
    bool inTime = false;
    while (i < s.size())
    {
        if (s[i] == 'T' || s[i] == 't') { inTime = true; i++; continue; }
        if (!isdigit((unsigned char)s[i])) return false;
        int64_t n = 0;
        while (i < s.size() && isdigit((unsigned char)s[i])) n = n * 10 + (s[i++] - '0');
        if (i >= s.size()) return false;
        switch (toupper((unsigned char)s[i]))
        {
        case 'W': total += n * 604800; break;
        case 'D': total += n * 86400; break;
        case 'H': total += n * 3600; break;
        case 'M': total += inTime ? n * 60 : 0; break; // M means minutes only after T
        case 'S': total += n; break;
        default: return false;
        }
        i++;
    }
    secs = sign * total;
    return true;
}

// ─── recurrence ───────────────────────────────────────────────────────────────

struct IcsRRule
{
    enum Freq { NONE, DAILY, WEEKLY, MONTHLY, YEARLY };
    struct ByDay { int ord; int dow; };

    Freq     freq = NONE;
    long     interval = 1;
    long     count = -1;
    uint64_t until = 0;
    bool     hasUntil = false;
    int      wkst = 0;
    std::vector<ByDay> byday;
};

bool parse_byday(const std::string &tok, int &ord, int &dow)
{
    static const char *W[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    size_t i = 0;
    int sign = 1;
    if (i < tok.size() && (tok[i] == '+' || tok[i] == '-'))
    {
        if (tok[i] == '-') sign = -1;
        i++;
    }
    int n = 0;
    bool haveN = false;
    while (i < tok.size() && isdigit((unsigned char)tok[i]))
    {
        n = n * 10 + (tok[i++] - '0');
        haveN = true;
    }
    if (tok.size() < i + 2) return false;
    std::string dd = tok.substr(i, 2);
    mstr::toUpper(dd);
    for (int j = 0; j < 7; j++)
        if (dd == W[j])
        {
            dow = j;
            ord = haveN ? sign * n : 0;
            return true;
        }
    return false;
}

bool parse_rrule(const std::string &value, const PosixTz &tz, IcsRRule &r)
{
    for (auto &part : mstr::split(value, ';'))
    {
        size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string k = part.substr(0, eq);
        std::string v = part.substr(eq + 1);
        mstr::toUpper(k);

        if (k == "FREQ")
        {
            std::string f = v;
            mstr::toUpper(f);
            if (f == "DAILY") r.freq = IcsRRule::DAILY;
            else if (f == "WEEKLY") r.freq = IcsRRule::WEEKLY;
            else if (f == "MONTHLY") r.freq = IcsRRule::MONTHLY;
            else if (f == "YEARLY") r.freq = IcsRRule::YEARLY;
        }
        else if (k == "INTERVAL")
        {
            r.interval = strtol(v.c_str(), nullptr, 10);
            if (r.interval < 1) r.interval = 1; // INTERVAL=0 is seen in the wild
        }
        else if (k == "COUNT")
        {
            r.count = strtol(v.c_str(), nullptr, 10);
        }
        else if (k == "UNTIL")
        {
            ParsedTime pt;
            if (parse_datetime(v, pt))
            {
                // UNTIL is UTC when DTSTART is a date-time and a bare DATE when
                // DTSTART is a DATE; resolve() handles both.
                r.until = (uint64_t)resolve(pt, tz);
                if (pt.dateOnly) r.until += 86399; // a bare date includes that whole day
                r.hasUntil = true;
            }
        }
        else if (k == "BYDAY")
        {
            for (auto &tok : mstr::split(v, ','))
            {
                int ord, dow;
                std::string t = tok;
                mstr::trim(t);
                if (parse_byday(t, ord, dow)) r.byday.push_back({ord, dow});
            }
        }
        else if (k == "WKST")
        {
            int ord, dow;
            if (parse_byday(v, ord, dow)) r.wkst = dow;
        }
    }
    return r.freq != IcsRRule::NONE;
}

// Append the day numbers in month (y,m) matching `dow`. ord > 0 selects the nth
// from the start, ord < 0 the nth from the end, ord == 0 all of them.
void dows_in_month(int y, unsigned m, int ord, int dow, std::vector<int64_t> &out)
{
    const int64_t first = days_from_civil(y, m, 1);
    const unsigned dim = days_in_month(y, m);
    const int fwd = ((dow - weekday_from_days(first)) % 7 + 7) % 7;

    std::vector<int64_t> all;
    for (int64_t d = first + fwd; d < first + (int64_t)dim; d += 7)
        all.push_back(d);
    if (all.empty()) return;

    if (ord == 0)
    {
        for (auto d : all) out.push_back(d);
        return;
    }
    int idx = (ord > 0) ? ord - 1 : (int)all.size() + ord;
    if (idx >= 0 && idx < (int)all.size()) out.push_back(all[idx]);
}

/**
 * Generate occurrence start instants inside [winStart, winEnd).
 *
 * Iteration starts from DTSTART, but when there is no COUNT to honour it jumps
 * straight to the window - a FREQ=DAILY series begun in 2005 would otherwise
 * need thousands of steps to reach today. With COUNT, iteration must run from
 * the beginning, but it is then bounded by COUNT itself.
 *
 * `utcAnchored` distinguishes the two recurrence semantics in RFC 5545: a
 * DTSTART carrying Z or an explicit offset recurs at a fixed UTC instant, so its
 * local time shifts across a DST boundary, whereas a floating or TZID-qualified
 * DTSTART recurs at a fixed wall-clock time and its UTC instant shifts instead.
 * `sday` and `sh:smi:ss` must be expressed in whichever calendar that selects.
 */
void expand_rrule(const IcsRRule &r, const PosixTz &tz, int64_t sday,
                  int sh, int smi, int ss, bool allDay, bool utcAnchored, int64_t duration,
                  uint64_t winStart, uint64_t winEnd, std::vector<uint64_t> &starts)
{
    if (r.freq == IcsRRule::NONE) return;

    // Day number -> occurrence instant, in the anchoring the event calls for.
    auto instant_for = [&](int64_t day) -> uint64_t {
        if (utcAnchored) return (uint64_t)(day * 86400 + sh * 3600 + smi * 60 + ss);
        if (allDay) return (uint64_t)tz.from_local_days(day, 0, 0, 0);
        return (uint64_t)tz.from_local_days(day, sh, smi, ss);
    };

    const bool bounded = (r.count > 0);
    long generated = 0;
    int64_t k = 0;

    if (!bounded)
    {
        // Back the target off by the duration so a long occurrence that starts
        // before the window but overlaps it is not skipped.
        const int64_t target = (int64_t)winStart - (duration > 0 ? duration : 0);
        const int64_t targetDay = utcAnchored ? (target >= 0 ? target / 86400 : (target - 86399) / 86400)
                                              : tz.local_day(target);
        if (targetDay > sday)
        {
            switch (r.freq)
            {
            case IcsRRule::DAILY:
                k = (targetDay - sday) / r.interval;
                break;
            case IcsRRule::WEEKLY:
                k = ((targetDay - sday) / 7) / r.interval;
                break;
            case IcsRRule::MONTHLY:
            {
                int sy, ty;
                unsigned smo, sd, tmo, td;
                civil_from_days(sday, sy, smo, sd);
                civil_from_days(targetDay, ty, tmo, td);
                k = (((int64_t)ty * 12 + tmo) - ((int64_t)sy * 12 + smo)) / r.interval;
                break;
            }
            case IcsRRule::YEARLY:
            {
                int sy, ty;
                unsigned smo, sd, tmo, td;
                civil_from_days(sday, sy, smo, sd);
                civil_from_days(targetDay, ty, tmo, td);
                k = ((int64_t)ty - sy) / r.interval;
                break;
            }
            default: break;
            }
            if (k > 1) k -= 1; // step back one period for safety at the boundary
            if (k < 0) k = 0;
        }
    }

    int sy;
    unsigned smo, sd;
    civil_from_days(sday, sy, smo, sd);

    std::vector<int64_t> days;
    for (int64_t iter = 0; iter < ICS_MAX_ITER; iter++, k++)
    {
        days.clear();

        switch (r.freq)
        {
        case IcsRRule::DAILY:
            days.push_back(sday + k * r.interval);
            break;

        case IcsRRule::WEEKLY:
        {
            if (r.byday.empty())
                days.push_back(sday + k * r.interval * 7);
            else
            {
                const int64_t weekStart =
                    sday - (((weekday_from_days(sday) - r.wkst) % 7 + 7) % 7);
                const int64_t base = weekStart + k * r.interval * 7;
                for (auto &bd : r.byday)
                    days.push_back(base + (((bd.dow - r.wkst) % 7 + 7) % 7));
            }
            break;
        }

        case IcsRRule::MONTHLY:
        {
            const int64_t total = (int64_t)sy * 12 + (smo - 1) + k * r.interval;
            const int ty = (int)(total / 12);
            const unsigned tm = (unsigned)(total % 12) + 1;
            if (r.byday.empty())
            {
                if (sd <= days_in_month(ty, tm)) days.push_back(days_from_civil(ty, tm, sd));
            }
            else
                for (auto &bd : r.byday) dows_in_month(ty, tm, bd.ord, bd.dow, days);
            break;
        }

        case IcsRRule::YEARLY:
        {
            const int ty = sy + (int)(k * r.interval);
            if (r.byday.empty())
            {
                if (sd <= days_in_month(ty, smo)) days.push_back(days_from_civil(ty, smo, sd));
            }
            else
                for (auto &bd : r.byday) dows_in_month(ty, smo, bd.ord, bd.dow, days);
            break;
        }

        default:
            return;
        }

        std::sort(days.begin(), days.end());

        for (auto day : days)
        {
            if (day < sday) continue; // an occurrence never precedes DTSTART

            const uint64_t st = instant_for(day);

            // COUNT counts rule-generated instances before EXDATE removal
            // (RFC 5545 3.8.5.3), so it is tallied here.
            if (bounded)
            {
                if (generated >= r.count) return;
                generated++;
            }
            if (r.hasUntil && st > r.until) return;
            if (st >= winEnd) return; // occurrences are monotonic; nothing later can match

            const uint64_t en = st + (uint64_t)(duration > 0 ? duration : (allDay ? 86400 : 1));
            if (en > winStart) starts.push_back(st);
        }
    }
}

// ─── one VEVENT under construction ────────────────────────────────────────────

struct IcsEvent
{
    std::string uid, summary, location, description, status;
    std::vector<std::string> categories;

    uint64_t dtstart = 0, dtend = 0;
    bool     haveStart = false, haveEnd = false;
    bool     allDay = false, floating = false;
    std::string tzid;

    int64_t  duration = 0;
    bool     haveDuration = false;

    std::string rrule;
    std::vector<uint64_t> exdates;

    bool     haveRecurrenceId = false;
    uint64_t recurrenceId = 0;
};

// ─── streaming, unfolding line reader ─────────────────────────────────────────

class IcsLineReader
{
public:
    IcsLineReader(ICAL_HTTP_CLIENT_CLASS &c, bool wantDescription)
        : _c(c), _wantDescription(wantDescription) {}

    // Returns the next unfolded logical line, or false at end of feed.
    bool next(std::string &out)
    {
        for (;;)
        {
            std::string raw;
            if (!take_raw(raw))
            {
                if (_haveLogical && !_skipping)
                {
                    out.swap(_logical);
                    _logical.clear();
                    _haveLogical = false;
                    return true;
                }
                return false;
            }

            if (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t'))
            {
                // Continuation of the line before it.
                if (_haveLogical && !_skipping && _logical.size() + raw.size() <= ICS_MAX_LINE)
                    _logical.append(raw, 1, std::string::npos);
                continue;
            }

            const bool emit = _haveLogical && !_skipping;
            std::string prev;
            if (emit) prev.swap(_logical);

            _haveLogical = true;
            _skipping = skippable(raw);
            if (_skipping)
                _logical.clear(); // never accumulate a base64 ATTACH blob
            else
            {
                _logical = raw;
                if (_logical.size() > ICS_MAX_LINE) _logical.resize(ICS_MAX_LINE);
            }

            if (emit)
            {
                out.swap(prev);
                return true;
            }
        }
    }

private:
    // Properties dropped before their continuations are ever accumulated.
    bool skippable(const std::string &raw) const
    {
        size_t e = raw.find_first_of(";:");
        std::string n = raw.substr(0, e == std::string::npos ? raw.size() : e);
        mstr::toUpper(n);
        if (n.size() >= 2 && n[0] == 'X' && n[1] == '-') return true;
        if (n == "ATTACH" || n == "PHOTO" || n == "IMAGE" || n == "GEO") return true;
        if (!_wantDescription && n == "DESCRIPTION") return true;
        return false;
    }

    bool fill()
    {
        uint8_t buf[ICS_CHUNK];
        int n = _c.read(buf, sizeof(buf));
        if (n <= 0) return false;
        _pending.append((char *)buf, n);
        return true;
    }

    bool take_raw(std::string &out)
    {
        for (;;)
        {
            size_t nl = _pending.find('\n');
            if (nl != std::string::npos)
            {
                out = _pending.substr(0, nl);
                _pending.erase(0, nl + 1);
                if (!out.empty() && out[out.size() - 1] == '\r') out.erase(out.size() - 1);
                return true;
            }
            if (_eof)
            {
                if (_pending.empty()) return false;
                out.swap(_pending);
                _pending.clear();
                if (!out.empty() && out[out.size() - 1] == '\r') out.erase(out.size() - 1);
                return true;
            }
            if (!fill()) _eof = true;
        }
    }

    ICAL_HTTP_CLIENT_CLASS &_c;
    bool _wantDescription;
    std::string _pending;
    std::string _logical;
    bool _haveLogical = false;
    bool _skipping = false;
    bool _eof = false;
};

// Case-insensitive whole-line compare, for BEGIN:/END: markers.
bool line_is(const std::string &line, const char *what)
{
    std::string l = line;
    mstr::trim(l);
    mstr::toUpper(l);
    return l == what;
}

} // namespace

// ─── construction ─────────────────────────────────────────────────────────────

NetworkProtocolICAL::NetworkProtocolICAL(std::string *rx_buf, std::string *tx_buf,
                                         std::string *sp_buf)
    : NetworkProtocolCalendar(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolICAL::ctor\r\n");
}

NetworkProtocolICAL::~NetworkProtocolICAL()
{
    Debug_printf("NetworkProtocolICAL::dtor\r\n");
}

// ─── provider hooks ───────────────────────────────────────────────────────────

fujiError_t NetworkProtocolICAL::connect_and_auth()
{
    std::string scheme = opened_url->scheme;
    mstr::toUpper(scheme);
    _tls = (scheme != "ICALH");
    return FUJI_ERROR::NONE;
}

std::string NetworkProtocolICAL::feed_url(const std::string &selector) const
{
    // The selector is re-sent verbatim: a percent-encoded secret feed URL must
    // survive unchanged.
    return std::string(_tls ? "https://" : "http://") + selector;
}

fujiError_t NetworkProtocolICAL::calendar_list(const std::string &selector,
                                               std::vector<CalendarListEntry> &out)
{
    // There is nothing to enumerate without a feed to fetch.
    Debug_printf("ICAL: no feed given; expected ICAL://host/path/to/feed.ics/...\r\n");
    error = NDEV_STATUS::INVALID_DEVICESPEC;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolICAL::event_index(const std::string &selector, uint64_t winStart,
                                             uint64_t winEnd, const std::string &categoryFilter,
                                             size_t maxCount, std::vector<CalendarEventEntry> &out)
{
    if (selector.empty())
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return FUJI_ERROR::UNSPECIFIED;
    }
    return scan_feed(selector, winStart, winEnd, categoryFilter, maxCount, &out, nullptr, 0,
                     nullptr);
}

fujiError_t NetworkProtocolICAL::event_detail(const std::string &selector,
                                              const CalendarEventEntry &ev,
                                              std::string &description)
{
    description.clear();
    // A second pass, this time keeping DESCRIPTION and looking for one event.
    // The window is widened by a day on each side so an occurrence that the
    // index clipped to the window edge still matches on its own start.
    const uint64_t lo = ev.start > 86400 ? ev.start - 86400 : 0;
    return scan_feed(selector, lo, ev.start + 86400, "", 1, nullptr, &ev.uid, ev.start,
                     &description);
}

void NetworkProtocolICAL::calendar_error_to_error()
{
    // A hook that already diagnosed something specific - a missing feed, a body
    // this build cannot decompress - keeps its own status; `error` is reset to
    // SUCCESS at the start of every open, so nothing stale can survive here.
    if (error != NDEV_STATUS::SUCCESS) return;
    error = http_status_to_error(_last_http);
}

// ─── the streaming pass ───────────────────────────────────────────────────────

fujiError_t NetworkProtocolICAL::scan_feed(const std::string &selector, uint64_t winStart,
                                           uint64_t winEnd, const std::string &categoryFilter,
                                           size_t maxCount, std::vector<CalendarEventEntry> *out,
                                           const std::string *wantUid, uint64_t wantStart,
                                           std::string *descOut)
{
    const std::string url = feed_url(selector);
    Debug_printf("ICAL: GET %s\r\n", url.c_str());

    ICAL_HTTP_CLIENT_CLASS client;
    if (!client.begin(url))
    {
        _last_http = 0;
        return FUJI_ERROR::UNSPECIFIED;
    }

    client.create_empty_stored_headers({"Content-Encoding"});
    client.set_header("Accept", "text/calendar, text/plain;q=0.5, */*;q=0.1");
    // Never advertise compression: neither client decompresses a response body.
    client.set_header("Accept-Encoding", "identity");

    _last_http = client.GET();
    if (_last_http < 200 || _last_http >= 300)
    {
        Debug_printf("ICAL: HTTP %d for %s\r\n", _last_http, url.c_str());
        client.close();
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::string enc = client.get_header("Content-Encoding");
    mstr::toLower(enc);
    if (!enc.empty() && enc != "identity")
    {
        Debug_printf("ICAL: refusing Content-Encoding: %s\r\n", enc.c_str());
        error = NDEV_STATUS::NOT_IMPLEMENTED;
        client.close();
        return FUJI_ERROR::UNSPECIFIED;
    }

    const PosixTz &zone = tz();
    const bool wantDescription = (descOut != nullptr);

    IcsLineReader reader(client, wantDescription);

    // RECURRENCE-ID overrides may appear anywhere in the feed, including before
    // their master event, so they are stashed and applied at end of feed.
    struct Override
    {
        std::string uid;
        uint64_t    recurrenceId;
        CalendarEventEntry ev;
        std::string description;
        bool        cancelled;
    };
    std::vector<Override> overrides;

    std::vector<CalendarEventEntry> collected;
    std::string foundDescription;
    bool found = false;

    // Keep the `maxCount` chronologically earliest events, not the first
    // `maxCount` the feed happens to mention. An AGENDA of the next 6 events
    // would otherwise be filled by whichever VEVENTs appear first in the file.
    size_t worst = (size_t)-1; // index of the latest-starting kept event, when known
    auto consider = [&](const CalendarEventEntry &e) {
        if (collected.size() < maxCount)
        {
            collected.push_back(e);
            worst = (size_t)-1;
            return;
        }
        if (collected.empty()) return;
        if (worst == (size_t)-1)
        {
            worst = 0;
            for (size_t i = 1; i < collected.size(); i++)
                if (collected[i].start > collected[worst].start) worst = i;
        }
        if (e.start < collected[worst].start)
        {
            collected[worst] = e;
            worst = (size_t)-1;
        }
    };

    IcsEvent cur;
    bool inEvent = false;
    std::string line;

    while (reader.next(line))
    {
        if (line_is(line, "BEGIN:VEVENT"))
        {
            cur = IcsEvent();
            inEvent = true;
            continue;
        }

        if (!inEvent)
            continue;

        if (line_is(line, "END:VEVENT"))
        {
            inEvent = false;
            if (!cur.haveStart) continue;

            // Resolve the event's end. An absent DTEND means DURATION, or a
            // whole day for an all-day event, or a zero-length instant.
            uint64_t evEnd;
            if (cur.haveEnd)
                evEnd = cur.dtend;
            else if (cur.haveDuration)
                evEnd = cur.dtstart + (uint64_t)(cur.duration > 0 ? cur.duration : 0);
            else if (cur.allDay)
                evEnd = (uint64_t)zone.from_local_days(zone.local_day((int64_t)cur.dtstart) + 1, 0, 0, 0);
            else
                evEnd = cur.dtstart;
            const int64_t duration = (int64_t)evEnd - (int64_t)cur.dtstart;

            // Category filter and display value.
            std::string catJoined;
            for (size_t i = 0; i < cur.categories.size(); i++)
            {
                if (i) catJoined += ",";
                catJoined += cur.categories[i];
            }
            if (!categoryFilter.empty())
            {
                bool hit = false;
                for (auto &c : cur.categories)
                {
                    std::string a = c;
                    if (mstr::equals(a, categoryFilter.c_str(), false)) { hit = true; break; }
                }
                if (!hit) continue;
            }

            std::string statusUpper = cur.status;
            mstr::toUpper(statusUpper);
            const bool cancelled = (statusUpper == "CANCELLED");

            CalendarEventEntry base;
            base.summary = cur.summary;
            base.location = cur.location;
            base.category = catJoined;
            base.uid = cur.uid;
            base.allDay = cur.allDay;
            base.floating = cur.floating;
            base.tzid = cur.tzid;

            if (cur.haveRecurrenceId)
            {
                if (overrides.size() < ICS_MAX_OVERRIDES)
                {
                    base.start = cur.dtstart;
                    base.end = evEnd;
                    base.recurring = true;
                    Override ov;
                    ov.uid = cur.uid;
                    ov.recurrenceId = cur.recurrenceId;
                    ov.ev = base;
                    ov.description = cur.description;
                    ov.cancelled = cancelled;
                    overrides.push_back(ov);
                }
                continue;
            }

            if (cancelled) continue;

            std::vector<uint64_t> starts;
            if (!cur.rrule.empty())
            {
                IcsRRule r;
                if (parse_rrule(cur.rrule, zone, r))
                {
                    // DTSTART is decomposed in UTC when it is absolute and in
                    // local time when it floats; see expand_rrule.
                    const bool utcAnchored = !cur.floating && !cur.allDay;
                    int y, h, mi, s, wd = 0;
                    unsigned mo, d;
                    if (utcAnchored)
                    {
                        const int64_t day = (int64_t)(cur.dtstart / 86400);
                        const int rem = (int)(cur.dtstart % 86400);
                        civil_from_days(day, y, mo, d);
                        h = rem / 3600;
                        mi = (rem % 3600) / 60;
                        s = rem % 60;
                    }
                    else
                        zone.to_local((int64_t)cur.dtstart, y, mo, d, h, mi, s, wd);

                    expand_rrule(r, zone, days_from_civil(y, mo, d), h, mi, s, cur.allDay,
                                 utcAnchored, duration, winStart, winEnd, starts);
                }
                else if (cur.dtstart < winEnd && evEnd > winStart)
                    starts.push_back(cur.dtstart); // unparseable rule: at least show the base
                base.recurring = true;
            }
            else if (cur.dtstart < winEnd &&
                     (evEnd > winStart || (evEnd == cur.dtstart && cur.dtstart >= winStart)))
            {
                starts.push_back(cur.dtstart);
            }

            for (auto st : starts)
            {
                if (std::find(cur.exdates.begin(), cur.exdates.end(), st) != cur.exdates.end())
                    continue;

                CalendarEventEntry e = base;
                e.start = st;
                e.end = st + (uint64_t)(duration > 0 ? duration : 0);

                if (descOut)
                {
                    if (wantUid && e.uid == *wantUid && st == wantStart)
                    {
                        foundDescription = cur.description;
                        found = true;
                    }
                }
                else
                    consider(e);
            }
            continue;
        }

        IcsProp p;
        if (!parse_prop(line, p)) continue;

        if (p.name == "UID")
            cur.uid = ics_unescape(p.value);
        else if (p.name == "SUMMARY")
            cur.summary = ics_unescape(p.value);
        else if (p.name == "LOCATION")
            cur.location = ics_unescape(p.value);
        else if (p.name == "DESCRIPTION")
            cur.description = ics_unescape(p.value);
        else if (p.name == "STATUS")
            cur.status = p.value;
        else if (p.name == "CATEGORIES")
        {
            for (auto &c : ics_split_list(p.value))
                if (!c.empty()) cur.categories.push_back(c);
        }
        else if (p.name == "RRULE")
            cur.rrule = p.value;
        else if (p.name == "DTSTART" || p.name == "DTEND" || p.name == "RECURRENCE-ID")
        {
            ParsedTime pt;
            if (!parse_datetime(p.value, pt)) continue;

            std::string vt = param_value(p.params, "VALUE");
            mstr::toUpper(vt);
            const bool dateOnly = pt.dateOnly || vt == "DATE";
            const uint64_t when = (uint64_t)resolve(pt, zone);

            if (p.name == "DTSTART")
            {
                cur.dtstart = when;
                cur.haveStart = true;
                cur.allDay = dateOnly;
                // TZID cannot be resolved without a timezone database; the value
                // is kept so a VTIMEZONE pass can use it later.
                cur.tzid = param_value(p.params, "TZID");
                cur.floating = !pt.utc && !pt.hasOffset;
            }
            else if (p.name == "DTEND")
            {
                cur.dtend = when;
                cur.haveEnd = true;
            }
            else
            {
                cur.recurrenceId = when;
                cur.haveRecurrenceId = true;
            }
        }
        else if (p.name == "DURATION")
        {
            if (ics_duration(p.value, cur.duration)) cur.haveDuration = true;
        }
        else if (p.name == "EXDATE")
        {
            for (auto &tok : ics_split_list(p.value))
            {
                ParsedTime pt;
                if (parse_datetime(tok, pt)) cur.exdates.push_back((uint64_t)resolve(pt, zone));
            }
        }
    }

    client.close();

    // Apply RECURRENCE-ID overrides now that the whole feed has been seen.
    if (!overrides.empty())
    {
        for (auto &ov : overrides)
        {
            if (descOut)
            {
                if (wantUid && ov.uid == *wantUid && ov.recurrenceId == wantStart && !ov.cancelled)
                {
                    foundDescription = ov.description;
                    found = true;
                }
                continue;
            }

            for (size_t i = 0; i < collected.size(); i++)
            {
                if (collected[i].uid != ov.uid || collected[i].start != ov.recurrenceId) continue;
                if (ov.cancelled)
                    collected.erase(collected.begin() + i);
                else
                    collected[i] = ov.ev;
                break;
            }
        }
    }

    if (descOut)
    {
        *descOut = foundDescription;
        if (!found) Debug_printf("ICAL: no DESCRIPTION found for the requested event\r\n");
        return FUJI_ERROR::NONE;
    }

    out->swap(collected);
    Debug_printf("ICAL: %u events in window\r\n", (unsigned)out->size());
    return FUJI_ERROR::NONE;
}
