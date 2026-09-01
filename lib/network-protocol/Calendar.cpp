/**
 * NetworkProtocolCalendar - base class for calendar protocol adapters.
 */

#include "Calendar.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "../utils/string_utils.h"
#include "status_error_codes.h"
#include "text_format.h"

using namespace fnfmt;
using namespace fn_time;

// All human-readable output is terminated with `lineEnding`, the per-device
// end-of-line set by the network.cpp layer. Because the output is built with it
// already applied, no further EOL translation is performed on top.

// Upper bound on events materialised for one window, to bound work and memory.
#define CAL_MAX_EVENTS 300
// Default number of events an AGENDA view returns.
#define CAL_DEFAULT_AGENDA 20
// A DIR listing is normally followed by a /N detail open; caching the window
// index for a short time makes that a single provider round-trip. Only one
// window is kept, and only when it is small enough to be cheap on ESP32.
#define CAL_CACHE_TTL 120
#define CAL_CACHE_MAX_EVENTS 150
// Upper bound on a composed/edited event draft.
#define CAL_MAX_WRITE 16384

namespace {

const char *MON3[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *MONFULL[] = {"", "January", "February", "March", "April", "May", "June",
                         "July", "August", "September", "October", "November", "December"};
const char *DOW3[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *DOW2[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

int digits(uint64_t n)
{
    int w = 1;
    while (n >= 10) { n /= 10; w++; }
    return w;
}

// Turn a built column line into its dashed header equivalent.
std::string dashed(const std::string &s)
{
    std::string h = s;
    for (auto &c : h)
        if (c == ' ') c = '-';
    return h;
}

// The single cached window. Function-local statics avoid static-init ordering.
struct CalCache
{
    std::string key;
    int64_t     stamp = 0;
    std::vector<CalendarEventEntry> items;
};

CalCache &window_cache()
{
    static CalCache cache;
    return cache;
}

// A committed create/edit changes the provider's data; a stale window would
// otherwise be served for up to CAL_CACHE_TTL afterwards.
void clear_window_cache()
{
    CalCache &c = window_cache();
    c.key.clear();
    c.items.clear();
    c.items.shrink_to_fit();
}

} // namespace

// ─── construction ─────────────────────────────────────────────────────────────

NetworkProtocolCalendar::NetworkProtocolCalendar(std::string *rx_buf, std::string *tx_buf,
                                                 std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolCalendar::ctor\r\n");

    _count = CAL_DEFAULT_AGENDA;

    // Per-platform default human-readable line width, matching Mailbox.
#if defined(BUILD_ATARI)
    _defaultWidth = 38;
#elif defined(BUILD_APPLE)
    _defaultWidth = 40;
#elif defined(BUILD_ADAM)
    _defaultWidth = 32;
#elif defined(BUILD_COCO)
    _defaultWidth = 32;
#elif defined(BUILD_RS232)
    _defaultWidth = 80;
#else
    _defaultWidth = 40;
#endif
}

NetworkProtocolCalendar::~NetworkProtocolCalendar()
{
    Debug_printf("NetworkProtocolCalendar::dtor\r\n");
}

// ─── devicespec parsing ───────────────────────────────────────────────────────

bool NetworkProtocolCalendar::view_from_string(const std::string &s, CalendarView &out)
{
    std::string v = s;
    mstr::toUpper(v);
    if (v == "DAY")    { out = CalendarView::DAY;    return true; }
    if (v == "WEEK")   { out = CalendarView::WEEK;   return true; }
    if (v == "MONTH")  { out = CalendarView::MONTH;  return true; }
    if (v == "AGENDA") { out = CalendarView::AGENDA; return true; }
    return false;
}

std::string NetworkProtocolCalendar::query_param(const std::string &key, const std::string &def) const
{
    size_t pos = 0;
    while (pos < _query.size())
    {
        size_t amp = _query.find('&', pos);
        std::string pair = _query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.compare(0, eq, key) == 0)
            return mstr::urlDecode(pair.substr(eq + 1), true); // '+' means space in a query
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return def;
}

std::string NetworkProtocolCalendar::selector_decoded() const
{
    // alter_pluses must be false: a calendar named "C++ Study" would otherwise
    // come back as "C   Study".
    return mstr::urlDecode(_selector, false);
}

bool NetworkProtocolCalendar::parse_devicespec(const std::string &rawUrl)
{
    // PeoplesUrlParser cannot be used for the path here. processAuthority()
    // splits on the first '@', which silently destroys a Google calendar id like
    // "abc@group.calendar.google.com", and cleanPath() canonicalises away parts
    // of an opaque provider selector. mRawUrl is preserved verbatim by
    // resetURL(), so parse that instead.
    std::string rest = rawUrl;
    size_t p = rest.find("://");
    if (p != std::string::npos) rest = rest.substr(p + 3);

    size_t q = rest.find_first_of("?#");
    if (q != std::string::npos)
    {
        _query = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }

    // Drop one leading slash so that GCAL:///Work and ICAL://host/f.ics both
    // put their first meaningful component in segment 0.
    if (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
    while (!rest.empty() && rest[rest.size() - 1] == '/') rest.erase(rest.size() - 1);

    std::vector<std::string> seg;
    if (!rest.empty())
    {
        size_t s = 0;
        for (;;)
        {
            size_t e = rest.find('/', s);
            if (e == std::string::npos) { seg.push_back(rest.substr(s)); break; }
            seg.push_back(rest.substr(s, e - s));
            s = e + 1;
        }
    }

    // Query parameters, read before the window is computed.
    _category = query_param("category");
    std::string cs = query_param("count");
    if (!cs.empty())
    {
        long c = strtol(cs.c_str(), nullptr, 10);
        if (c > 0) _count = (size_t)c;
    }
    std::string ds = query_param("days");
    if (!ds.empty())
    {
        long d = strtol(ds.c_str(), nullptr, 10);
        if (d > 0 && d <= 3660) _horizonDays = (int)d;
    }
    std::string wk = query_param("wkst");
    mstr::toUpper(wk);
    if (wk == "MO" || wk == "1") _wkst = 1;

    std::string tzs = query_param("tz");
    if (tzs.empty()) tzs = Config.get_general_timezone();
    if (!_tz.parse(tzs))
    {
        Debug_printf("Calendar: unusable timezone \"%s\", falling back to UTC\r\n", tzs.c_str());
        _tz = PosixTz();
    }

    // An explicit ?view= bypasses path scanning entirely.
    size_t viewIdx = seg.size();
    std::string dateStr;

    std::string qv = query_param("view");
    if (!qv.empty() && view_from_string(qv, _view))
    {
        _haveView = true;
        dateStr = query_param("date");
        std::string qn = query_param("n");
        if (!qn.empty()) _eventNum = strtol(qn.c_str(), nullptr, 10);
    }
    else
    {
        // Scan the tail, longest first. The grammar is VIEW[/DATE[/N]], so only
        // the last three segments can hold it. Scanning forward would misfire on
        // a legitimate feed path such as /calendars/day/team.ics/DAY/2026-08-28.
        for (int k = 3; k >= 1 && !_haveView; k--)
        {
            if ((int)seg.size() < k) continue;
            size_t i = seg.size() - k;

            CalendarView v;
            if (!view_from_string(seg[i], v)) continue;

            std::string d;
            if (k >= 2)
            {
                d = seg[i + 1];
                ParsedTime pt;
                bool ok = parse_datetime(d, pt);
                if (!ok && v == CalendarView::MONTH) ok = parse_yearmonth(d, pt);
                if (!ok) continue;
            }

            long n = -1;
            if (k == 3)
            {
                const std::string &ns = seg[i + 2];
                if (ns.empty() || ns.find_first_not_of("0123456789") != std::string::npos) continue;
                n = strtol(ns.c_str(), nullptr, 10);
                if (n < 1) continue;
            }

            viewIdx = i;
            _view = v;
            _haveView = true;
            dateStr = d;
            _eventNum = n;
        }
    }

    for (size_t i = 0; i < viewIdx; i++)
    {
        if (i) _selector += '/';
        _selector += seg[i];
    }

    return compute_window(dateStr);
}

bool NetworkProtocolCalendar::compute_window(const std::string &dateStr)
{
    const int64_t now = (int64_t)time(nullptr);

    int64_t anchor = _tz.local_day(now);
    int y;
    unsigned mo, d;
    civil_from_days(anchor, y, mo, d);
    bool explicitDate = false;

    if (!dateStr.empty())
    {
        ParsedTime pt;
        bool ok = parse_datetime(dateStr, pt);
        if (!ok && _view == CalendarView::MONTH) ok = parse_yearmonth(dateStr, pt);
        if (!ok) return false;
        y = pt.year;
        mo = pt.month;
        d = pt.day;
        anchor = days_from_civil(y, mo, d);
        explicitDate = true;
    }

    // Window edges are always re-derived from civil dates - never start+86400 -
    // so that 23- and 25-hour DST days come out right.
    switch (_view)
    {
    case CalendarView::DAY:
        _winStart = (uint64_t)_tz.from_local_days(anchor, 0, 0, 0);
        _winEnd = (uint64_t)_tz.from_local_days(anchor + 1, 0, 0, 0);
        break;

    case CalendarView::WEEK:
    {
        int back = ((weekday_from_days(anchor) - _wkst) % 7 + 7) % 7;
        int64_t d0 = anchor - back;
        _winStart = (uint64_t)_tz.from_local_days(d0, 0, 0, 0);
        _winEnd = (uint64_t)_tz.from_local_days(d0 + 7, 0, 0, 0);
        break;
    }

    case CalendarView::MONTH:
    {
        int ny = y;
        unsigned nm = mo + 1;
        if (nm > 12) { nm = 1; ny++; }
        _winStart = (uint64_t)_tz.from_local_days(days_from_civil(y, mo, 1), 0, 0, 0);
        _winEnd = (uint64_t)_tz.from_local_days(days_from_civil(ny, nm, 1), 0, 0, 0);
        break;
    }

    case CalendarView::AGENDA:
    default:
        _winStart = explicitDate ? (uint64_t)_tz.from_local_days(anchor, 0, 0, 0) : (uint64_t)now;
        _winEnd = (uint64_t)_tz.from_local_days(anchor + _horizonDays, 0, 0, 0);
        break;
    }

    return _winEnd > _winStart;
}

// ─── open / dispatch ──────────────────────────────────────────────────────────

fujiError_t NetworkProtocolCalendar::open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                                          netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);
    error = NDEV_STATUS::SUCCESS;
    receiveBuffer->clear();

    bool isDir = (access == ACCESS_MODE::DIRECTORY || access == ACCESS_MODE::DIRECTORY_ALT);
    // IEC leaves channel_aux1 at READWRITE by default (iec/network.cpp), so a
    // plain C64 LOAD arrives as 12. Treat it as a read rather than erroring.
    bool isRead = (access == ACCESS_MODE::READ || access == ACCESS_MODE::READWRITE);
    bool isWrite = (access == ACCESS_MODE::WRITE);
    if (!isDir && !isRead && !isWrite)
    {
        // APPEND (and anything else) makes no sense here.
        error = NDEV_STATUS::READ_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }
    if (isWrite && !can_write())
    {
        error = NDEV_STATUS::READ_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }
    if (access == ACCESS_MODE::READWRITE)
        Debug_printf("Calendar: aux1=READWRITE treated as READ\r\n");

    if (!parse_devicespec(urlParser->mRawUrl))
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (connect_and_auth() != FUJI_ERROR::NONE)
    {
        calendar_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    fujiError_t res;
    if (isWrite)
    {
        if (_eventNum >= 0)
        {
            // Edit: resolve N now, so a provider change between open and the
            // commit at close cannot renumber the events underneath it.
            std::vector<CalendarEventEntry> items;
            res = fetch_events(items);
            if (res != FUJI_ERROR::NONE) return res;
            if (_eventNum < 1 || (size_t)_eventNum > items.size())
            {
                error = NDEV_STATUS::FILE_NOT_FOUND;
                return FUJI_ERROR::UNSPECIFIED;
            }
            _editTarget = items[(size_t)_eventNum - 1];
            _isEdit = true;
        }
        else if (_haveView)
        {
            // A period without an event number is nothing writable.
            error = NDEV_STATUS::INVALID_DEVICESPEC;
            return FUJI_ERROR::UNSPECIFIED;
        }
        _writeMode = true;
        res = FUJI_ERROR::NONE;
    }
    else if (!_haveView && _selector.empty())
    {
        res = do_calendar_list((uint8_t)translate, isDir);
    }
    else if (_eventNum >= 0)
    {
        if (isDir)
        {
            error = NDEV_STATUS::INVALID_DEVICESPEC;
            res = FUJI_ERROR::UNSPECIFIED;
        }
        else
            res = do_event_detail();
    }
    else
    {
        res = do_event_index((uint8_t)translate, isDir);
    }

    // Every path above emits `lineEnding` itself, so translating again would
    // double-apply the EOL.
    translation_mode = NETPROTO_TRANS_NONE;
    forceStatus = true;
    return res;
}

fujiError_t NetworkProtocolCalendar::do_calendar_list(uint8_t transByte, bool isDir)
{
    std::vector<CalendarListEntry> items;
    if (calendar_list(_selector, items) != FUJI_ERROR::NONE)
    {
        calendar_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (!isDir)
    {
        *receiveBuffer = std::to_string(items.size()) + lineEnding;
        return FUJI_ERROR::NONE;
    }

    // aux2 == 255 selects raw binary; anything else is human-readable, with the
    // line width taken from the low 7 bits (0 -> platform default).
    if (transByte == 0xFF)
        format_list_raw(items);
    else
    {
        int width = transByte & 0x7F;
        format_list_human(items, width ? width : _defaultWidth);
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCalendar::do_event_index(uint8_t transByte, bool isDir)
{
    std::vector<CalendarEventEntry> items;
    fujiError_t r = fetch_events(items);
    if (r != FUJI_ERROR::NONE) return r;

    if (!isDir)
    {
        *receiveBuffer = std::to_string(items.size()) + lineEnding;
        return FUJI_ERROR::NONE;
    }

    if (transByte == 0xFF)
        format_index_raw(items);
    else
    {
        int width = transByte & 0x7F;
        format_index_human(items, width ? width : _defaultWidth);
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCalendar::do_event_detail()
{
    std::vector<CalendarEventEntry> items;
    fujiError_t r = fetch_events(items);
    if (r != FUJI_ERROR::NONE) return r;

    if (_eventNum < 1 || (size_t)_eventNum > items.size())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    const CalendarEventEntry &ev = items[(size_t)_eventNum - 1];
    std::string description;
    if (event_detail(_selector, ev, description) != FUJI_ERROR::NONE)
    {
        calendar_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    *receiveBuffer = format_detail(ev, description, _defaultWidth);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCalendar::fetch_events(std::vector<CalendarEventEntry> &out)
{
    CalCache &cache = window_cache();

    const std::string key = opened_url->scheme + "|" + _selector + "|" +
                            std::to_string(_winStart) + "|" + std::to_string(_winEnd) + "|" +
                            _category + "|" + std::to_string(_count) + "|" +
                            std::to_string((int)_view);
    const int64_t now = (int64_t)time(nullptr);

    if (cache.key == key && now - cache.stamp <= CAL_CACHE_TTL)
    {
        Debug_printf("Calendar: window cache hit (%u events)\r\n", (unsigned)cache.items.size());
        out = cache.items;
        return FUJI_ERROR::NONE;
    }

    size_t cap = (_view == CalendarView::AGENDA) ? _count : (size_t)CAL_MAX_EVENTS;
    if (cap > CAL_MAX_EVENTS) cap = CAL_MAX_EVENTS;

    out.clear();
    if (event_index(_selector, _winStart, _winEnd, _category, cap, out) != FUJI_ERROR::NONE)
    {
        calendar_error_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    // A total order, so that N addresses the same event on a later open. The
    // protocol object does not survive between opens, so /N re-runs this query
    // and the provider's own ordering cannot be relied upon.
    std::sort(out.begin(), out.end(),
              [](const CalendarEventEntry &a, const CalendarEventEntry &b) {
                  if (a.start != b.start) return a.start < b.start;
                  if (a.end != b.end) return a.end < b.end;
                  if (a.uid != b.uid) return a.uid < b.uid;
                  return a.summary < b.summary;
              });

    if (out.size() > cap) out.resize(cap);
    for (size_t i = 0; i < out.size(); i++)
        out[i].eventNum = (uint32_t)(i + 1);

    if (out.size() <= CAL_CACHE_MAX_EVENTS)
    {
        cache.key = key;
        cache.stamp = now;
        cache.items = out;
    }
    else
    {
        cache.key.clear();
        cache.items.clear();
        cache.items.shrink_to_fit();
    }

    return FUJI_ERROR::NONE;
}

nDevStatus_t NetworkProtocolCalendar::http_status_to_error(int code)
{
    if (code <= 0) return NDEV_STATUS::NETWORK_UNREACHABLE; // nothing was sent
    if (code >= 900)                                        // not an HTTP status
        return (code == 901) ? NDEV_STATUS::CONNECTION_REFUSED : NDEV_STATUS::CONNECTION_RESET;

    switch (code)
    {
    case 401:
    case 403: return NDEV_STATUS::ACCESS_DENIED;
    case 404:
    case 410: return NDEV_STATUS::FILE_NOT_FOUND;
    case 408: return NDEV_STATUS::GENERAL_TIMEOUT;
    case 429: return NDEV_STATUS::SERVICE_NOT_AVAILABLE;
    default: break;
    }
    if (code >= 500) return NDEV_STATUS::SERVER_GENERAL;
    if (code >= 400) return NDEV_STATUS::CLIENT_GENERAL;
    return NDEV_STATUS::GENERAL;
}

// ─── rendering helpers ────────────────────────────────────────────────────────

int NetworkProtocolCalendar::date_column_width() const
{
    switch (_view)
    {
    case CalendarView::DAY:    return 0; // every event is on the same day
    case CalendarView::WEEK:   return 3; // "Fri"
    case CalendarView::MONTH:  return 5; // "Fr 28"
    case CalendarView::AGENDA:
    default:                   return 6; // "28 Aug"
    }
}

std::string NetworkProtocolCalendar::event_date_column(uint64_t t) const
{
    int y, h, mi, s, wd;
    unsigned mo, d;
    _tz.to_local((int64_t)t, y, mo, d, h, mi, s, wd);

    char buf[16];
    switch (_view)
    {
    case CalendarView::DAY:
        return "";
    case CalendarView::WEEK:
        return DOW3[wd & 7];
    case CalendarView::MONTH:
        snprintf(buf, sizeof(buf), "%s %02u", DOW2[wd & 7], d);
        return buf;
    case CalendarView::AGENDA:
    default:
        snprintf(buf, sizeof(buf), "%02u %s", d, MON3[mo]);
        return buf;
    }
}

std::string NetworkProtocolCalendar::event_time_column(const CalendarEventEntry &ev) const
{
    if (ev.allDay) return "all day";

    int y1, h1, m1, s1, w1, y2, h2, m2, s2, w2;
    unsigned mo1, d1, mo2, d2;
    _tz.to_local((int64_t)ev.start, y1, mo1, d1, h1, m1, s1, w1);
    _tz.to_local((int64_t)(ev.end ? ev.end - 1 : ev.start), y2, mo2, d2, h2, m2, s2, w2);

    char buf[16];
    if (y1 != y2 || mo1 != mo2 || d1 != d2)
    {
        // Ends on a later day; showing that end time would read as same-day.
        snprintf(buf, sizeof(buf), "%02d:%02d->", h1, m1);
        return buf;
    }

    _tz.to_local((int64_t)ev.end, y2, mo2, d2, h2, m2, s2, w2);
    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", h1, m1, h2, m2);
    return buf;
}

std::string NetworkProtocolCalendar::window_title() const
{
    int y, h, mi, s, wd;
    unsigned mo, d;
    _tz.to_local((int64_t)_winStart, y, mo, d, h, mi, s, wd);

    char buf[64];
    switch (_view)
    {
    case CalendarView::DAY:
        snprintf(buf, sizeof(buf), "%s %02u %s %04d", DOW3[wd & 7], d, MON3[mo], y);
        break;
    case CalendarView::WEEK:
        snprintf(buf, sizeof(buf), "Week of %s %02u %s %04d", DOW3[wd & 7], d, MON3[mo], y);
        break;
    case CalendarView::MONTH:
        snprintf(buf, sizeof(buf), "%s %04d", MONFULL[mo], y);
        break;
    case CalendarView::AGENDA:
    default:
        snprintf(buf, sizeof(buf), "Agenda from %02u %s %04d", d, MON3[mo], y);
        break;
    }
    return buf;
}

void NetworkProtocolCalendar::append_wrapped(std::string &out, const std::string &text,
                                             int width) const
{
    if (width < 8) width = 8;

    std::string line;
    size_t i = 0;
    while (i <= text.size())
    {
        char c = (i < text.size()) ? text[i] : '\n';
        if (c == '\r') { i++; continue; }

        if (c == '\n')
        {
            // A newline in the text always breaks the line, preserving blank
            // lines; the synthetic one at end-of-text only flushes leftovers, so
            // text already ending in a newline does not gain a trailing blank.
            if (i < text.size() || !line.empty())
            {
                out += line;
                out += lineEnding;
            }
            line.clear();
            if (i >= text.size()) break;
            i++;
            continue;
        }

        line += c;
        if ((int)line.size() >= width)
        {
            // Break at the last space when there is one, so words stay whole.
            size_t brk = line.find_last_of(' ');
            if (brk != std::string::npos && brk > 0)
            {
                out += line.substr(0, brk);
                line = line.substr(brk + 1);
            }
            else
            {
                out += line;
                line.clear();
            }
            out += lineEnding;
        }
        i++;
    }
}

// ─── formatting ───────────────────────────────────────────────────────────────

void NetworkProtocolCalendar::format_index_human(const std::vector<CalendarEventEntry> &items,
                                                 int width)
{
    // Narrow screens get the two-line layout the Mailbox message index uses:
    // line 1 is padded to exactly lineW so that on a 40-column Atari the
    // auto-wrap lands on the half boundary and the title reads as the second
    // row. Once the line is wide enough to hold the title as well, a single row
    // per event is easier to scan, so the columns collapse onto one line.
    int lineW = (width >= 16) ? width : _defaultWidth;

    uint32_t maxNum = 1;
    for (auto &it : items)
        if (it.eventNum > maxNum) maxNum = it.eventNum;
    const int numW = digits(maxNum);
    const int dateW = date_column_width();
    const int timeW = 11; // "09:00-10:00"

    const int used = 2 + numW + 1 + (dateW ? dateW + 1 : 0) + timeW;
    const bool wide = (lineW - used) >= 40; // room for a category and a title

    int catW, sumW = 0;
    if (wide)
    {
        catW = 14;
        sumW = lineW - used - catW - 2;
    }
    else
    {
        catW = lineW - used - 1;
        if (catW < 4) catW = 0;
    }

    std::string out;
    out.reserve(items.size() * (size_t)(lineW + 24) + 128);

    out += ellipsize(window_title(), lineW);
    out += lineEnding;

    {
        std::string h = "  ";
        h += rjust("#", numW);
        h += ' ';
        if (dateW) { h += ljust("Date", dateW); h += ' '; }
        h += ljust("Time", timeW);
        if (catW) { h += ' '; h += ljust("Category", catW); }
        if (wide) { h += ' '; h += ljust("Event", sumW); }
        out += dashed(ljust(h, lineW));
        out += lineEnding;
    }

    if (items.empty())
    {
        out += "  (no events)";
        out += lineEnding;
        *receiveBuffer = out;
        return;
    }

    for (auto &it : items)
    {
        std::string l1;
        l1 += it.allDay ? '*' : (it.recurring ? '~' : ' ');
        l1 += ' ';
        l1 += rjust(std::to_string(it.eventNum), numW);
        l1 += ' ';
        if (dateW) { l1 += ljust(event_date_column(it.start), dateW); l1 += ' '; }
        l1 += ljust(event_time_column(it), timeW);
        if (catW) { l1 += ' '; l1 += ljust(ellipsize(it.category, catW), catW); }

        std::string body = it.summary.empty() ? "(no title)" : it.summary;
        if (!it.location.empty()) body += " @" + it.location;

        if (wide)
        {
            l1 += ' ';
            l1 += ellipsize(body, sumW);
            out += l1;
        }
        else
        {
            out += ljust(l1, lineW);
            out += "  ";
            out += ellipsize(body, lineW - 2);
        }
        out += lineEnding;
    }

    *receiveBuffer = out;
}

void NetworkProtocolCalendar::format_index_raw(const std::vector<CalendarEventEntry> &items)
{
    std::string out;
    out.reserve(items.size() * sizeof(CalEventItem));
    for (auto &it : items)
    {
        append_le(out, it.eventNum, 4);
        append_le(out, it.start, 8);
        append_le(out, it.end, 8);
        out += (char)((it.allDay ? CAL_FLAG_ALLDAY : 0) | (it.recurring ? CAL_FLAG_RECURRING : 0));
        append_fixed(out, it.summary, 96);
        append_fixed(out, it.location, 64);
        append_fixed(out, it.category, 32);
        append_fixed(out, it.uid, 64);
    }
    *receiveBuffer = out;
}

void NetworkProtocolCalendar::format_list_human(const std::vector<CalendarListEntry> &items,
                                                int width)
{
    int lineW = (width >= 16) ? width : _defaultWidth;

    int catW = lineW / 3;
    if (catW < 6) catW = 6;
    int nameW = lineW - catW - 1;
    if (nameW < 6) { nameW = 6; catW = (lineW - 7 > 0) ? lineW - 7 : 1; }

    std::string out;
    out.reserve(items.size() * (size_t)(lineW + 2) + 64);

    {
        std::string h = ljust("Calendar", nameW);
        h += ' ';
        h += ljust("Category", catW);
        out += dashed(ljust(h, lineW));
        out += lineEnding;
    }

    if (items.empty())
    {
        out += "(none)";
        out += lineEnding;
    }

    for (auto &it : items)
    {
        out += ljust(ellipsize(it.name.empty() ? it.id : it.name, nameW), nameW);
        out += ' ';
        out += ellipsize(it.category, catW);
        out += lineEnding;
    }

    *receiveBuffer = out;
}

void NetworkProtocolCalendar::format_list_raw(const std::vector<CalendarListEntry> &items)
{
    std::string out;
    out.reserve(items.size() * sizeof(CalListItem));
    for (auto &it : items)
    {
        append_fixed(out, it.name, 64);
        append_fixed(out, it.category, 32);
        append_fixed(out, it.id, 128);
    }
    *receiveBuffer = out;
}

std::string NetworkProtocolCalendar::format_detail(const CalendarEventEntry &ev,
                                                   const std::string &description, int width)
{
    int lineW = (width >= 16) ? width : _defaultWidth;

    int y, h, mi, s, wd;
    unsigned mo, d;
    _tz.to_local((int64_t)ev.start, y, mo, d, h, mi, s, wd);

    std::string out;
    out += ev.summary.empty() ? "(no title)" : ev.summary;
    out += lineEnding;

    char when[80];
    if (ev.allDay)
    {
        // An all-day end is exclusive; show the last day the event covers.
        int64_t lastDay = _tz.local_day((int64_t)(ev.end > ev.start ? ev.end - 1 : ev.start));
        int ey;
        unsigned emo, ed;
        civil_from_days(lastDay, ey, emo, ed);
        if (ey == y && emo == mo && ed == d)
            snprintf(when, sizeof(when), "%s %02u %s %04d - all day", DOW3[wd & 7], d, MON3[mo], y);
        else
            snprintf(when, sizeof(when), "%02u %s %04d - %02u %s %04d, all day",
                     d, MON3[mo], y, ed, MON3[emo], ey);
    }
    else
    {
        int ey, eh, emi, es, ewd;
        unsigned emo, ed;
        _tz.to_local((int64_t)ev.end, ey, emo, ed, eh, emi, es, ewd);
        if (ey == y && emo == mo && ed == d)
            snprintf(when, sizeof(when), "%s %02u %s %04d %02d:%02d-%02d:%02d",
                     DOW3[wd & 7], d, MON3[mo], y, h, mi, eh, emi);
        else
            snprintf(when, sizeof(when), "%02u %s %04d %02d:%02d - %02u %s %04d %02d:%02d",
                     d, MON3[mo], y, h, mi, ed, MON3[emo], ey, eh, emi);
    }
    append_wrapped(out, when, lineW);

    if (ev.recurring)
    {
        out += "Repeats";
        out += lineEnding;
    }
    if (!ev.category.empty())
        append_wrapped(out, "Category: " + ev.category, lineW);
    if (!ev.location.empty())
        append_wrapped(out, "Where: " + ev.location, lineW);

    if (!description.empty())
    {
        out += lineEnding;
        append_wrapped(out, description, lineW);
    }

    return out;
}

// ─── write channel (compose / edit) ───────────────────────────────────────────

fujiError_t NetworkProtocolCalendar::event_create(const std::string &, const CalendarEventDraft &)
{
    error = NDEV_STATUS::READ_ONLY;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolCalendar::event_update(const CalendarEventEntry &, const CalendarEventDraft &)
{
    error = NDEV_STATUS::READ_ONLY;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolCalendar::write(unsigned short len)
{
    was_write = true;

    size_t n = len;
    if (n > transmitBuffer->length()) n = transmitBuffer->length();

    if (!_writeMode)
    {
        transmitBuffer->erase(0, n);
        error = NDEV_STATUS::READ_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }
    if (_writeBuf.size() + n > CAL_MAX_WRITE)
    {
        transmitBuffer->erase(0, n);
        _writeFailed = true;
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    _writeBuf.append(*transmitBuffer, 0, n);
    transmitBuffer->erase(0, n);
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCalendar::close()
{
    fujiError_t res = FUJI_ERROR::NONE;
    if (_writeMode && !_committed)
    {
        _committed = true;
        if (!transmitBuffer->empty())
            write((unsigned short)std::min(transmitBuffer->length(), (size_t)0xFFFF));
        res = commit_write();
    }

    // The base close resets `error`; keep the commit result visible so the bus
    // layer can latch it into the STATUS that follows the close.
    nDevStatus_t saved = error;
    NetworkProtocol::close();
    error = saved;
    return res;
}

fujiError_t NetworkProtocolCalendar::commit_write()
{
    if (_writeBuf.empty())
    {
        // Opened for write, closed without writing: an abort, not an error.
        error = NDEV_STATUS::SUCCESS;
        return FUJI_ERROR::NONE;
    }
    if (_writeFailed)
    {
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    CalendarEventDraft draft;
    CalDraftError derr = cal_draft_parse(_writeBuf, draft);
    if (derr == CalDraftError::NONE)
    {
        if (_isEdit)
        {
            CalendarDraftTimes existing;
            existing.allDay = _editTarget.allDay;
            existing.startEpoch = (int64_t)_editTarget.start;
            existing.endEpoch = (int64_t)_editTarget.end;
            existing.startDay = _tz.local_day(existing.startEpoch);
            // An all-day end is an exclusive local midnight already.
            existing.endDayExcl = _tz.local_day(existing.endEpoch);
            derr = cal_draft_finalize(draft, _tz, &existing);
        }
        else
            derr = cal_draft_finalize(draft, _tz, nullptr);
    }
    if (derr != CalDraftError::NONE)
    {
        Debug_printf("Calendar: draft rejected (%d)\r\n", (int)derr);
        error = NDEV_STATUS::INVALID_COMMAND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    fujiError_t res = _isEdit ? event_update(_editTarget, draft)
                              : event_create(_selector, draft);
    if (res != FUJI_ERROR::NONE)
    {
        calendar_error_to_error();
        return res;
    }

    clear_window_cache();
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

// ─── read / status / available ────────────────────────────────────────────────

fujiError_t NetworkProtocolCalendar::read(unsigned short len)
{
    if (_writeMode)
    {
        error = NDEV_STATUS::WRITE_ONLY;
        return FUJI_ERROR::UNSPECIFIED;
    }
    // All content is staged into receiveBuffer at open(); the device drains it.
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolCalendar::status(NetworkStatus *status)
{
    if (_writeMode)
    {
        // A write channel is never at EOF; report health, not drain state.
        status->error = error;
        status->connected = 1;
        return FUJI_ERROR::NONE;
    }
    if (error == NDEV_STATUS::SUCCESS && receiveBuffer->empty())
        status->error = NDEV_STATUS::END_OF_FILE;
    else
        status->error = error;
    status->connected = receiveBuffer->empty() ? 0 : 1;
    return FUJI_ERROR::NONE;
}

size_t NetworkProtocolCalendar::available()
{
    return receiveBuffer->size();
}
