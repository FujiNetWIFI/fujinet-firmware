/**
 * Base interface for Protocol adapters that read and write calendars.
 *
 * A sibling of NetworkProtocolFS and NetworkProtocolMailbox: it abstracts
 * reading a day, week, month or agenda view of a calendar and emits
 * human-readable (or, on request, raw binary) output for calendar programs and
 * utilities running on the retro host.
 *
 * Concrete subclasses (GCAL, ICAL) supply only the provider data layer via the
 * pure-virtual hooks below; this base owns the OPEN/STATUS/READ/CLOSE template,
 * all date arithmetic, and all shared formatting.
 *
 * Devicespec / operation model:
 *
 *   <SCHEME>://<selector>/<VIEW>[/<DATE>[/<N>]]
 *
 *   /                          mode 6 (DIR)   -> list of calendars / categories
 *   /                          mode 4 (READ)  -> number of calendars
 *   /<sel>/<VIEW>[/<DATE>]     mode 6 (DIR)   -> event index for the period
 *   /<sel>/<VIEW>[/<DATE>]     mode 4 (READ)  -> event count for the period
 *   /<sel>/<VIEW>/<DATE>/<N>   mode 4 (READ)  -> detail of event N
 *   /<sel>                     mode 8 (WRITE) -> compose a new event
 *   /<sel>/<VIEW>/<DATE>/<N>   mode 8 (WRITE) -> edit event N of the period
 *
 * VIEW is DAY, WEEK, MONTH or AGENDA. DATE is YYYY-MM-DD (or YYYY-MM for
 * MONTH); omitting it means today. N is the event's 1-based position in the
 * period's listing. The listing is sorted by (start, end, uid, summary) in this
 * base class so that N means the same thing across two separate opens - the
 * protocol object does not survive between them.
 *
 * The VIEW keyword is located by scanning the path from the END, because a
 * selector may legitimately contain a segment named "day".
 *
 * Query params: category=<name> filters, count=<n> bounds AGENDA (default 20),
 * days=<n> sets the AGENDA horizon (default 365), wkst=SU|MO sets the first day
 * of the week, tz=<posix> overrides the configured timezone, and
 * view=/date=/n= override path scanning entirely.
 *
 * For DIR ops, aux2 (translate) selects the output format: 255 = raw binary
 * structs; any other value = human-readable, with the line width taken from the
 * low 7 bits (0 -> default). NOS opens a directory with aux2=128, which yields a
 * human-readable listing at the default width.
 *
 * For READ ops aux2 is ignored, and event detail is rendered at the platform
 * default width. Unlike Mailbox, which streams a message body through the aux2
 * EOL translation, every byte here is composed by this class with `lineEnding`
 * already applied - translating on top of that would double-apply the EOL.
 *
 * Write model: the host writes text field lines - "SUMMARY: Lunch",
 * "START: 2026-09-01 12:00", END, LOCATION, DESCRIPTION (repeatable),
 * CATEGORY - and CLOSE commits through the event_create / event_update hooks.
 * Keys are case-insensitive; lines may end with 0x9B, CR, LF or CRLF; parsing
 * and validation live in calendar_draft.{h,cpp}. A date-only START makes an
 * all-day event; a missing END defaults to one hour (timed) or one day
 * (all-day); an all-day END names the last covered day, inclusive. An edit
 * changes only the fields written; START alone moves the event, preserving
 * its duration. Providers that cannot write (ICAL) leave can_write() false
 * and a WRITE open fails with READ_ONLY.
 *
 * Write caveats: a calendar literally NAMED day/week/month/agenda cannot be
 * compose-targeted by name (the view scan claims the segment) - use its id.
 * Indexes expand recurring events, so editing N edits that occurrence only.
 * A raw-struct write form (aux2=255) is not implemented; it would decode into
 * the same CalendarEventDraft ahead of the same hooks.
 */

#ifndef NETWORKPROTOCOL_CALENDAR
#define NETWORKPROTOCOL_CALENDAR

#include "Protocol.h"

#include "calendar_draft.h"

#include "../utils/fn_time.h"

#include <cstdint>
#include <string>
#include <vector>

// Flag bits in the raw wire format.
#define CAL_FLAG_ALLDAY    0x01
#define CAL_FLAG_RECURRING 0x02

#pragma pack(push, 1)
// Raw index entry emitted when an event index is opened with DIR + aux2 == 255.
typedef struct _calEventItem
{
    uint32_t eventNum;
    uint64_t start;    // seconds since epoch, UTC
    uint64_t end;      // exclusive
    uint8_t  flags;    // CAL_FLAG_*
    char     summary[96];
    char     location[64];
    char     category[32];
    char     uid[64];
} CalEventItem;

// Raw entry emitted when the calendar list is opened with DIR + aux2 == 255.
typedef struct _calListItem
{
    char name[64];
    char category[32];
    char id[128];
} CalListItem;
#pragma pack(pop)

static_assert(sizeof(CalEventItem) == 277, "CalEventItem wire layout changed");
static_assert(sizeof(CalListItem) == 224, "CalListItem wire layout changed");

enum class CalendarView
{
    DAY,
    WEEK,
    MONTH,
    AGENDA
};

// Structured event the provider hooks fill in (host-native types).
struct CalendarEventEntry
{
    uint32_t    eventNum = 0; // assigned by the base after sorting
    std::string summary;
    std::string location;
    std::string category;
    std::string uid;
    std::string providerId;   // provider's own handle, when it differs from uid
    uint64_t    start = 0;    // seconds since epoch, UTC
    uint64_t    end = 0;      // exclusive
    bool        allDay = false;
    bool        recurring = false;
    // Reserved for resolving VTIMEZONE-named times; unresolved floating times
    // are interpreted in the configured zone for now.
    bool        floating = false;
    std::string tzid;
};

// Structured calendar/category the provider hooks fill in.
struct CalendarListEntry
{
    std::string id;
    std::string name;
    std::string category;
};

class NetworkProtocolCalendar : public NetworkProtocol
{
public:
    NetworkProtocolCalendar(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolCalendar();

    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                     netProtoTranslation_t translate) override;
    fujiError_t read(unsigned short len) override;
    fujiError_t write(unsigned short len) override;
    fujiError_t close() override;
    fujiError_t status(NetworkStatus *status) override;
    size_t      available() override;

    // Default human-readable line width, used when the width parameter (aux2
    // low 7 bits) is 0. Defaulted per platform in the ctor; settable here.
    void setDefaultHumanWidth(int w) { if (w > 0) _defaultWidth = w; }

protected:
    // ---- provider hooks (implemented by GCAL / ICAL) ----

    // Connect and authenticate. Called once at open, before any fetch.
    virtual fujiError_t connect_and_auth() = 0;

    // Calendars (or categories) this provider can offer for the given selector.
    virtual fujiError_t calendar_list(const std::string &selector,
                                      std::vector<CalendarListEntry> &out) = 0;

    // Events intersecting [winStart, winEnd). Descriptions are NOT wanted here.
    // `categoryFilter` is empty for "any". `maxCount` bounds the result.
    // Ordering does not matter; the base sorts and numbers the result.
    virtual fujiError_t event_index(const std::string &selector, uint64_t winStart, uint64_t winEnd,
                                    const std::string &categoryFilter, size_t maxCount,
                                    std::vector<CalendarEventEntry> &out) = 0;

    // Long description for one event. May return an empty string.
    virtual fujiError_t event_detail(const std::string &selector, const CalendarEventEntry &ev,
                                     std::string &description) = 0;

    // Map the last provider error into `error` (nDevStatus_t).
    virtual void calendar_error_to_error() = 0;

    // Whether this provider can create and modify events. Gates WRITE opens.
    virtual bool can_write() const { return false; }

    // Create a new event in the calendar named by `selector`. The draft is
    // validated and its times finalized. Default: calendars are read-only.
    virtual fujiError_t event_create(const std::string &selector, const CalendarEventDraft &d);

    // Apply the draft's fields to the existing event `ev`. Default: read-only.
    virtual fujiError_t event_update(const CalendarEventEntry &ev, const CalendarEventDraft &d);

    // ---- parsed request state (populated by open) ----

    // Everything before the VIEW keyword, exactly as it appeared in the
    // devicespec. ICAL re-sends this verbatim; GCAL wants selector_decoded().
    std::string _selector;
    std::string _query; // raw query string, without the leading '?'

    CalendarView _view = CalendarView::AGENDA;
    bool         _haveView = false;
    long         _eventNum = -1; // 1-based; -1 when no /N was given

    std::string      _category;
    size_t           _count = 20;   // AGENDA event cap
    int              _horizonDays = 365;
    int              _wkst = 0;     // 0 = Sunday, 1 = Monday
    fn_time::PosixTz _tz;

    uint64_t _winStart = 0;
    uint64_t _winEnd = 0;

    int _defaultWidth = 40;

    // Percent-decoded selector, for providers that match on a human name.
    std::string selector_decoded() const;

    // Look up a key in `_query`. Values are percent-decoded.
    std::string query_param(const std::string &key, const std::string &def = "") const;

    // Wall-clock helpers in the request's timezone, for provider-side use.
    const fn_time::PosixTz &tz() const { return _tz; }

    // Map an HTTP status onto a device status. Handles the out-of-band codes the
    // client wrappers use for transport failures (mgHttpClient reports 900/901/902,
    // and 0 or -1 when nothing was sent), which are not HTTP statuses at all.
    static nDevStatus_t http_status_to_error(int code);

private:
    // Devicespec parsing.
    bool parse_devicespec(const std::string &rawUrl);
    bool compute_window(const std::string &dateStr);
    static bool view_from_string(const std::string &s, CalendarView &out);

    // Operation dispatch.
    fujiError_t do_calendar_list(uint8_t transByte, bool isDir);
    fujiError_t do_event_index(uint8_t transByte, bool isDir);
    fujiError_t do_event_detail();

    // Fetch + sort + number, with a small single-window cache so that a DIR
    // followed by a /N detail open does not re-query the provider.
    fujiError_t fetch_events(std::vector<CalendarEventEntry> &out);

    // ---- write channel (compose / edit) ----

    bool _writeMode = false;
    bool _isEdit = false;      // edit target resolved at open
    bool _committed = false;   // commit runs once even if close() re-enters
    bool _writeFailed = false; // an overflowed write must not commit truncated data
    CalendarEventEntry _editTarget;
    std::string _writeBuf;

    // Parse + validate the accumulated draft and dispatch to the provider hook.
    fujiError_t commit_write();

    // Shared formatting.
    void format_index_human(const std::vector<CalendarEventEntry> &items, int width);
    void format_index_raw(const std::vector<CalendarEventEntry> &items);
    void format_list_human(const std::vector<CalendarListEntry> &items, int width);
    void format_list_raw(const std::vector<CalendarListEntry> &items);
    std::string format_detail(const CalendarEventEntry &ev, const std::string &description,
                              int width);

    // Rendering helpers, all in the request's timezone.
    std::string window_title() const;
    std::string event_date_column(uint64_t t) const;
    std::string event_time_column(const CalendarEventEntry &ev) const;
    int         date_column_width() const;

    // Append `text`, translating embedded newlines to `lineEnding` and wrapping
    // to `width` columns.
    void append_wrapped(std::string &out, const std::string &text, int width) const;
};

#endif /* NETWORKPROTOCOL_CALENDAR */
