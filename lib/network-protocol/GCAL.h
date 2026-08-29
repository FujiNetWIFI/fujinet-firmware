#ifndef NETWORKPROTOCOLGCAL_H
#define NETWORKPROTOCOLGCAL_H

#include "Calendar.h"

#include <cJSON.h>
#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#define GCAL_HTTP_CLIENT_CLASS fnHttpClient
#else
#include "../http/mgHttpClient.h"
#define GCAL_HTTP_CLIENT_CLASS mgHttpClient
#endif

/**
 * NetworkProtocolGCAL
 *
 * Google Calendar adapter for FujiNet.
 *
 * URL format:  GCAL:///                     (list of calendars)
 *              GCAL:///DAY/2026-08-28       (all shown calendars, merged)
 *              GCAL:///Work/WEEK            (one calendar, by name or id)
 *              GCAL:///[star]/MONTH/2026-08 (every calendar, shown or not)
 *
 * ...where [star] is a literal asterisk, spelled out here only because it would
 * otherwise close this comment.
 *
 * The selector matches a calendar's name case-insensitively; anything that does
 * not match a name is used as a literal calendarId. An empty selector merges the
 * calendars marked "selected" in the Google UI, each event carrying its
 * calendar's name as its category.
 *
 * Authentication reuses the existing Google Drive OAuth grant stored in
 * fnConfig [GoogleDrive]; the grant's scope must include calendar.readonly.
 * Tokens are refreshed automatically via the relay, exactly like GDRIVE/GMAIL.
 *
 * Recurrences are expanded by Google (singleEvents=true), so no client-side
 * RRULE handling is needed here.
 */
class NetworkProtocolGCAL : public NetworkProtocolCalendar
{
public:
    NetworkProtocolGCAL(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolGCAL();

    NetworkProtocolGCAL(const NetworkProtocolGCAL &) = delete;
    NetworkProtocolGCAL &operator=(const NetworkProtocolGCAL &) = delete;

protected:
    fujiError_t connect_and_auth() override;
    fujiError_t calendar_list(const std::string &selector,
                              std::vector<CalendarListEntry> &out) override;
    fujiError_t event_index(const std::string &selector, uint64_t winStart, uint64_t winEnd,
                            const std::string &categoryFilter, size_t maxCount,
                            std::vector<CalendarEventEntry> &out) override;
    fujiError_t event_detail(const std::string &selector, const CalendarEventEntry &ev,
                             std::string &description) override;
    void calendar_error_to_error() override;

private:
    std::string _access_token;
    int  _last_http = 0;
    bool _scope_problem = false; // 403 that reads like a missing/undelivered scope

    // ---- OAuth (reuses the shared Google grant / relay, as GMAIL does) ----
    bool ensure_access_token();
    bool refresh_access_token();

    // ---- HTTP / JSON helpers ----
    std::string api_get(const std::string &url);
    std::string api_post(const std::string &url, const std::string &body,
                         const std::string &content_type);
    static std::string url_encode(const std::string &s);
    static std::string json_str(cJSON *obj, const char *key);

    // ---- Calendar helpers ----
    // Fetch the account's calendar list. `selectedOnly` keeps just the ones the
    // Google UI is showing.
    bool fetch_calendars(bool selectedOnly, std::vector<CalendarListEntry> &out);

    // Resolve a selector to the calendar ids it names.
    bool resolve_calendars(const std::string &selector, std::vector<CalendarListEntry> &out);

    // One calendar's events in [winStart, winEnd), appended to `out`.
    bool fetch_events(const CalendarListEntry &cal, uint64_t winStart, uint64_t winEnd,
                      const std::string &categoryFilter, size_t maxCount,
                      std::vector<CalendarEventEntry> &out);

    // Category for one event, by the agreed precedence:
    // extendedProperties.private -> extendedProperties.shared -> colorId -> calendar name.
    std::string category_for(cJSON *item, const CalendarListEntry &cal);

    // RFC 3339 UTC timestamp, as Google's timeMin/timeMax want it.
    static std::string rfc3339_utc(uint64_t t);

    static constexpr const char *GCAL_BASE = "https://www.googleapis.com/calendar/v3";
};

#endif /* NETWORKPROTOCOLGCAL_H */
