#ifndef NETWORKPROTOCOLICAL_H
#define NETWORKPROTOCOLICAL_H

#include "Calendar.h"

#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#define ICAL_HTTP_CLIENT_CLASS fnHttpClient
#else
#include "../http/mgHttpClient.h"
#define ICAL_HTTP_CLIENT_CLASS mgHttpClient
#endif

/**
 * NetworkProtocolICAL
 *
 * iCalendar (RFC 5545) feed adapter for FujiNet.
 *
 * URL format:  ICAL://host/path/to/feed.ics/DAY/2026-08-28
 *              WEBCAL://...                              (alias for ICAL)
 *              ICALH://host/path/to/feed.ics/WEEK        (plain http)
 *
 * ICAL and WEBCAL fetch over https; ICALH fetches over http. Everything before
 * the view keyword is the host and path of the feed, re-sent verbatim, so a
 * percent-encoded secret feed URL survives unchanged.
 *
 * The feed is parsed as it streams: logical lines are unfolded a chunk at a
 * time, VEVENTs are matched against the requested window as they are seen, and
 * recurrences are expanded only within that window. Peak memory is therefore a
 * function of the window, not of the feed size.
 *
 * TZID-named floating times cannot be resolved without a timezone database, so
 * they are interpreted in the configured timezone; ?tz=<posix> overrides that.
 */
class NetworkProtocolICAL : public NetworkProtocolCalendar
{
public:
    NetworkProtocolICAL(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolICAL();

    NetworkProtocolICAL(const NetworkProtocolICAL &) = delete;
    NetworkProtocolICAL &operator=(const NetworkProtocolICAL &) = delete;

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
    bool _tls = true;   // false for the ICALH scheme
    int  _last_http = 0;

    std::string feed_url(const std::string &selector) const;

    /**
     * Single streaming pass over the feed.
     *
     * When `out` is non-null the pass collects every event intersecting
     * [winStart, winEnd) that passes `categoryFilter`, up to `maxCount`.
     * When `descOut` is non-null it instead looks for the single event matching
     * `wantUid` at `wantStart` and returns its DESCRIPTION.
     */
    fujiError_t scan_feed(const std::string &selector, uint64_t winStart, uint64_t winEnd,
                          const std::string &categoryFilter, size_t maxCount,
                          std::vector<CalendarEventEntry> *out,
                          const std::string *wantUid, uint64_t wantStart,
                          std::string *descOut);
};

#endif /* NETWORKPROTOCOLICAL_H */
