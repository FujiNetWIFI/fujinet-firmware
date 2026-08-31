/**
 * NetworkProtocolGCAL
 *
 * Google Calendar adapter. Reuses the Google Drive OAuth grant and relay, like
 * GMAIL, and talks to the Calendar v3 REST API.
 */

#include "GCAL.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "../utils/string_utils.h"
#include "status_error_codes.h"

using namespace fn_time;

// Token refresh goes through the same relay as Google Drive; the shared grant
// must carry the calendar.readonly scope for the returned token to reach the
// Calendar API, plus calendar.events for compose/edit.
#define GCAL_RELAY_REFRESH_URL "https://auth.fujinet.online/gdrive-refresh"

// Bound the work when a selector names every calendar in an account.
#define GCAL_MAX_CALENDARS 8
// Google caps maxResults at 2500; a smaller page keeps each response parseable
// on an ESP32 without a large transient allocation.
#define GCAL_PAGE_SIZE 250

namespace {

// Google's eleven named event colours, indexed by colorId.
const char *GCAL_COLORS[] = {"",          "Lavender", "Sage",      "Grape",
                             "Flamingo",  "Banana",   "Tangerine", "Peacock",
                             "Graphite",  "Blueberry", "Basil",    "Tomato"};

// Read a nested string, e.g. extendedProperties.private.categories.
std::string nested_str(cJSON *obj, const char *a, const char *b, const char *c)
{
    if (!obj) return "";
    cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, a);
    if (!n) return "";
    if (b)
    {
        n = cJSON_GetObjectItemCaseSensitive(n, b);
        if (!n) return "";
    }
    if (c)
    {
        n = cJSON_GetObjectItemCaseSensitive(n, c);
        if (!n) return "";
    }
    return (cJSON_IsString(n) && n->valuestring) ? n->valuestring : "";
}

} // namespace

// ─── construction ─────────────────────────────────────────────────────────────

NetworkProtocolGCAL::NetworkProtocolGCAL(std::string *rx_buf, std::string *tx_buf,
                                         std::string *sp_buf)
    : NetworkProtocolCalendar(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolGCAL::ctor\r\n");
}

NetworkProtocolGCAL::~NetworkProtocolGCAL()
{
    Debug_printf("NetworkProtocolGCAL::dtor\r\n");
}

// ─── small helpers ────────────────────────────────────────────────────────────

std::string NetworkProtocolGCAL::url_encode(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string NetworkProtocolGCAL::json_str(cJSON *obj, const char *key)
{
    if (!obj) return "";
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item)) return "";
    return item->valuestring ? item->valuestring : "";
}

std::string NetworkProtocolGCAL::civil_date_str(int64_t dayNum)
{
    int y;
    unsigned mo, d;
    civil_from_days(dayNum, y, mo, d);

    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, mo, d);
    return buf;
}

std::string NetworkProtocolGCAL::rfc3339_utc(uint64_t t)
{
    const int64_t days = (int64_t)(t / 86400);
    const int rem = (int)(t % 86400);
    int y;
    unsigned mo, d;
    civil_from_days(days, y, mo, d);

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02d:%02d:%02dZ", y, mo, d, rem / 3600,
             (rem % 3600) / 60, rem % 60);
    return buf;
}

// ─── OAuth2 token management (reuses the GDRIVE token storage / relay) ────────

bool NetworkProtocolGCAL::refresh_access_token()
{
    std::string refresh_token = Config.get_gdrive_refresh_token();
    if (refresh_token.empty())
    {
        Debug_printf("GCAL: no refresh token — authorize Google in the web UI\r\n");
        return false;
    }

    std::string body = "refresh_token=" + url_encode(refresh_token);
    std::string resp = api_post(GCAL_RELAY_REFRESH_URL, body, "application/x-www-form-urlencoded");
    if (resp.empty()) return false;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return false;

    std::string token = json_str(j, "access_token");
    cJSON *expires_in = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    long ttl = (expires_in && cJSON_IsNumber(expires_in)) ? (long)expires_in->valuedouble : 3600;
    cJSON_Delete(j);

    if (token.empty()) return false;

    Config.store_gdrive_access_token(token);
    Config.store_gdrive_token_expiry((long)time(nullptr) + ttl);
    Config.save();

    _access_token = token;
    Debug_printf("GCAL: access token refreshed, expires in %ld s\r\n", ttl);
    return true;
}

bool NetworkProtocolGCAL::ensure_access_token()
{
    long now = (long)time(nullptr);
    if (now >= Config.get_gdrive_token_expiry() - 60)
        return refresh_access_token();
    _access_token = Config.get_gdrive_access_token();
    return !_access_token.empty();
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────
//
// Both client implementations expose the same begin/GET/POST/read surface, so
// unlike GDRIVE/GMAIL there is a single code path here rather than a per-platform
// pair.

std::string NetworkProtocolGCAL::api_get(const std::string &url)
{
    GCAL_HTTP_CLIENT_CLASS http;
    if (!http.begin(url))
    {
        _last_http = 0;
        return "";
    }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());

    _last_http = http.GET();

    std::string body;
    uint8_t buf[512];
    int n;
    while ((n = http.read(buf, sizeof(buf))) > 0)
        body.append((char *)buf, n);
    http.close();

    if (_last_http < 200 || _last_http >= 300)
    {
        Debug_printf("GCAL api_get: HTTP %d for %s: %s\r\n", _last_http, url.c_str(), body.c_str());
        if (_last_http == 401 || _last_http == 403)
            sniff_auth_body(body);
        return "";
    }
    return body;
}

void NetworkProtocolGCAL::sniff_auth_body(const std::string &body)
{
    std::string b = body;
    mstr::toLower(b);
    if (b.find("insufficient") != std::string::npos ||
        b.find("service_disabled") != std::string::npos ||
        b.find("accessnotconfigured") != std::string::npos)
    {
        _scope_problem = true;
        Debug_printf("GCAL: the Google grant is missing a calendar scope "
                     "(calendar.readonly / calendar.events) — re-authorize Google in the web UI\r\n");
    }
}

std::string NetworkProtocolGCAL::api_post(const std::string &url, const std::string &body,
                                          const std::string &content_type)
{
    return api_send(false, url, body, content_type);
}

std::string NetworkProtocolGCAL::api_send(bool patch, const std::string &url,
                                          const std::string &body, const std::string &content_type)
{
    GCAL_HTTP_CLIENT_CLASS http;
    if (!http.begin(url))
    {
        _last_http = 0;
        return "";
    }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    http.set_header("Content-Type", content_type.c_str());

    _last_http = patch ? http.PATCH(body.c_str(), (int)body.size())
                       : http.POST(body.c_str(), (int)body.size());

    std::string resp;
    uint8_t buf[512];
    int n;
    while ((n = http.read(buf, sizeof(buf))) > 0)
        resp.append((char *)buf, n);
    http.close();

    if (_last_http < 200 || _last_http >= 300)
    {
        Debug_printf("GCAL api_send: HTTP %d for %s: %s\r\n", _last_http, url.c_str(), resp.c_str());
        if (_last_http == 401 || _last_http == 403)
            sniff_auth_body(resp);
        return "";
    }
    return resp;
}

// ─── provider hooks ───────────────────────────────────────────────────────────

fujiError_t NetworkProtocolGCAL::connect_and_auth()
{
    if (!ensure_access_token())
    {
        error = NDEV_STATUS::ACCESS_DENIED;
        return FUJI_ERROR::UNSPECIFIED;
    }
    return FUJI_ERROR::NONE;
}

void NetworkProtocolGCAL::calendar_error_to_error()
{
    if (_scope_problem)
    {
        error = NDEV_STATUS::ACCESS_DENIED;
        return;
    }
    // A hook that already diagnosed something specific keeps its own status.
    if (error != NDEV_STATUS::SUCCESS) return;
    error = http_status_to_error(_last_http);
}

bool NetworkProtocolGCAL::fetch_calendars(bool selectedOnly, std::vector<CalendarListEntry> &out)
{
    std::string resp = api_get(std::string(GCAL_BASE) +
                               "/users/me/calendarList?minAccessRole=reader&maxResults=250"
                               "&fields=items(id,summary,summaryOverride,selected,primary)");
    if (resp.empty()) return false;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j)
    {
        error = NDEV_STATUS::COULD_NOT_PARSE_JSON;
        return false;
    }

    cJSON *items = cJSON_GetObjectItemCaseSensitive(j, "items");
    cJSON *it = nullptr;
    cJSON_ArrayForEach(it, items)
    {
        cJSON *sel = cJSON_GetObjectItemCaseSensitive(it, "selected");
        cJSON *pri = cJSON_GetObjectItemCaseSensitive(it, "primary");
        const bool isPrimary = cJSON_IsTrue(pri);
        // Google omits "selected" when it is false for a secondary calendar, but
        // the primary calendar is always shown.
        if (selectedOnly && !isPrimary && !cJSON_IsTrue(sel)) continue;

        CalendarListEntry e;
        e.id = json_str(it, "id");
        e.name = json_str(it, "summaryOverride");
        if (e.name.empty()) e.name = json_str(it, "summary");
        if (e.name.empty()) e.name = e.id;
        e.category = e.name;
        if (!e.id.empty()) out.push_back(e);
    }
    cJSON_Delete(j);
    return true;
}

fujiError_t NetworkProtocolGCAL::calendar_list(const std::string &selector,
                                               std::vector<CalendarListEntry> &out)
{
    if (!fetch_calendars(false, out)) return FUJI_ERROR::UNSPECIFIED;
    return FUJI_ERROR::NONE;
}

bool NetworkProtocolGCAL::resolve_calendars(const std::string &selector,
                                            std::vector<CalendarListEntry> &out)
{
    const std::string sel = selector_decoded();

    // "*" means every calendar; empty means the ones the Google UI is showing.
    if (sel.empty() || sel == "*")
    {
        if (!fetch_calendars(sel.empty(), out)) return false;
        if (out.size() > GCAL_MAX_CALENDARS)
        {
            Debug_printf("GCAL: %u calendars found, using the first %d\r\n", (unsigned)out.size(),
                         GCAL_MAX_CALENDARS);
            out.resize(GCAL_MAX_CALENDARS);
        }
        return true;
    }

    // A name match wins; otherwise the selector is taken as a literal id, so
    // that "primary" and full calendar ids both work without a list lookup.
    std::vector<CalendarListEntry> all;
    if (fetch_calendars(false, all))
    {
        for (auto &c : all)
        {
            std::string name = c.name;
            if (mstr::equals(name, sel.c_str(), false))
            {
                out.push_back(c);
                return true;
            }
        }
        for (auto &c : all)
        {
            std::string id = c.id;
            if (mstr::equals(id, sel.c_str(), false))
            {
                out.push_back(c);
                return true;
            }
        }
    }

    CalendarListEntry e;
    e.id = sel;
    e.name = sel;
    e.category = sel;
    out.push_back(e);
    return true;
}

std::string NetworkProtocolGCAL::category_for(cJSON *item, const CalendarListEntry &cal)
{
    // Precedence: explicit extended properties, then the event's colour, then
    // the calendar the event came from.
    std::string c = nested_str(item, "extendedProperties", "private", "categories");
    if (!c.empty()) return c;

    c = nested_str(item, "extendedProperties", "shared", "categories");
    if (!c.empty()) return c;

    std::string colorId = json_str(item, "colorId");
    if (!colorId.empty())
    {
        long n = strtol(colorId.c_str(), nullptr, 10);
        if (n >= 1 && n <= 11) return GCAL_COLORS[n];
    }

    return cal.category;
}

bool NetworkProtocolGCAL::fetch_events(const CalendarListEntry &cal, uint64_t winStart,
                                       uint64_t winEnd, const std::string &categoryFilter,
                                       size_t maxCount, std::vector<CalendarEventEntry> &out)
{
    std::string pageToken;

    do
    {
        size_t want = (maxCount > out.size()) ? maxCount - out.size() : 0;
        if (want == 0) break;
        if (want > GCAL_PAGE_SIZE) want = GCAL_PAGE_SIZE;

        std::string url = std::string(GCAL_BASE) + "/calendars/" + url_encode(cal.id) +
                          "/events?singleEvents=true&orderBy=startTime" +
                          "&timeMin=" + url_encode(rfc3339_utc(winStart)) +
                          "&timeMax=" + url_encode(rfc3339_utc(winEnd)) +
                          "&maxResults=" + std::to_string(want) +
                          "&fields=" +
                          url_encode("nextPageToken,items(id,summary,location,status,colorId,"
                                     "recurringEventId,start,end,extendedProperties)");
        if (!pageToken.empty()) url += "&pageToken=" + url_encode(pageToken);

        std::string resp = api_get(url);
        if (resp.empty()) return false;

        cJSON *j = cJSON_Parse(resp.c_str());
        if (!j)
        {
            error = NDEV_STATUS::COULD_NOT_PARSE_JSON;
            return false;
        }

        pageToken = json_str(j, "nextPageToken");

        cJSON *items = cJSON_GetObjectItemCaseSensitive(j, "items");
        cJSON *it = nullptr;
        cJSON_ArrayForEach(it, items)
        {
            std::string st = json_str(it, "status");
            if (mstr::equals(st, "cancelled", false)) continue;

            cJSON *jstart = cJSON_GetObjectItemCaseSensitive(it, "start");
            cJSON *jend = cJSON_GetObjectItemCaseSensitive(it, "end");
            if (!jstart) continue;

            CalendarEventEntry e;
            e.summary = json_str(it, "summary");
            e.location = json_str(it, "location");
            e.uid = json_str(it, "id");
            e.recurring = !json_str(it, "recurringEventId").empty();
            e.category = category_for(it, cal);
            // Both halves are percent-encoded, so the single '/' is unambiguous.
            e.providerId = url_encode(cal.id) + "/" + url_encode(e.uid);

            std::string sDate = json_str(jstart, "date");
            std::string sTime = json_str(jstart, "dateTime");
            ParsedTime pt;

            if (!sDate.empty())
            {
                // All-day: anchor to LOCAL midnight, or a UTC-offset user sees
                // the event on the day before.
                if (!parse_datetime(sDate, pt)) continue;
                e.allDay = true;
                e.start = (uint64_t)tz().from_local(pt.year, pt.month, pt.day, 0, 0, 0);

                // Google's end.date is exclusive, which is what `end` means here.
                std::string eDate = jend ? json_str(jend, "date") : "";
                if (!eDate.empty() && parse_datetime(eDate, pt))
                    e.end = (uint64_t)tz().from_local(pt.year, pt.month, pt.day, 0, 0, 0);
                else
                    e.end = (uint64_t)tz().from_local_days(tz().local_day((int64_t)e.start) + 1, 0, 0, 0);
            }
            else if (!sTime.empty())
            {
                if (!parse_datetime(sTime, pt)) continue;
                e.start = (uint64_t)resolve(pt, tz());

                std::string eTime = jend ? json_str(jend, "dateTime") : "";
                if (!eTime.empty() && parse_datetime(eTime, pt))
                    e.end = (uint64_t)resolve(pt, tz());
                else
                    e.end = e.start;
            }
            else
                continue;

            if (!categoryFilter.empty())
            {
                bool hit = false;
                for (auto &c : mstr::split(e.category, ','))
                {
                    std::string a = c;
                    mstr::trim(a);
                    if (mstr::equals(a, categoryFilter.c_str(), false)) { hit = true; break; }
                }
                if (!hit) continue;
            }

            out.push_back(e);
            if (out.size() >= maxCount) break;
        }
        cJSON_Delete(j);

    } while (!pageToken.empty() && out.size() < maxCount);

    return true;
}

fujiError_t NetworkProtocolGCAL::event_index(const std::string &selector, uint64_t winStart,
                                             uint64_t winEnd, const std::string &categoryFilter,
                                             size_t maxCount, std::vector<CalendarEventEntry> &out)
{
    std::vector<CalendarListEntry> cals;
    if (!resolve_calendars(selector, cals)) return FUJI_ERROR::UNSPECIFIED;

    for (auto &c : cals)
    {
        // Each calendar gets its own budget rather than sharing one running
        // total: with a shared budget the first calendar would fill a short
        // AGENDA outright and later calendars' earlier events would be lost.
        std::vector<CalendarEventEntry> mine;
        if (!fetch_events(c, winStart, winEnd, categoryFilter, maxCount, mine))
            return FUJI_ERROR::UNSPECIFIED;

        out.insert(out.end(), mine.begin(), mine.end());

        // Trim back to the earliest maxCount after each merge, so peak memory
        // stays at one calendar's worth above the cap however many are merged.
        if (out.size() > maxCount)
        {
            std::partial_sort(out.begin(), out.begin() + maxCount, out.end(),
                              [](const CalendarEventEntry &a, const CalendarEventEntry &b) {
                                  return a.start < b.start;
                              });
            out.resize(maxCount);
        }
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGCAL::event_detail(const std::string &selector,
                                              const CalendarEventEntry &ev,
                                              std::string &description)
{
    description.clear();

    size_t slash = ev.providerId.find('/');
    if (slash == std::string::npos) return FUJI_ERROR::NONE; // nothing to fetch against

    const std::string calId = ev.providerId.substr(0, slash);
    const std::string evId = ev.providerId.substr(slash + 1);

    std::string resp = api_get(std::string(GCAL_BASE) + "/calendars/" + calId + "/events/" + evId +
                               "?fields=description");
    if (resp.empty()) return FUJI_ERROR::UNSPECIFIED;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j)
    {
        error = NDEV_STATUS::COULD_NOT_PARSE_JSON;
        return FUJI_ERROR::UNSPECIFIED;
    }
    description = json_str(j, "description");
    cJSON_Delete(j);
    return FUJI_ERROR::NONE;
}

// ─── compose / edit ───────────────────────────────────────────────────────────

std::string NetworkProtocolGCAL::build_event_json(const CalendarEventDraft &d, bool forPatch)
{
    cJSON *j = cJSON_CreateObject();

    if (d.has_summary) cJSON_AddStringToObject(j, "summary", d.summary.c_str());
    if (d.has_location) cJSON_AddStringToObject(j, "location", d.location.c_str());
    if (d.has_description) cJSON_AddStringToObject(j, "description", d.description.c_str());
    if (d.has_category)
    {
        // Where the read side's category precedence looks first.
        cJSON *ep = cJSON_AddObjectToObject(j, "extendedProperties");
        cJSON *priv = cJSON_AddObjectToObject(ep, "private");
        cJSON_AddStringToObject(priv, "categories", d.category.c_str());
    }
    if (d.timesValid)
    {
        cJSON *start = cJSON_AddObjectToObject(j, "start");
        cJSON *end = cJSON_AddObjectToObject(j, "end");
        if (d.times.allDay)
        {
            cJSON_AddStringToObject(start, "date", civil_date_str(d.times.startDay).c_str());
            cJSON_AddStringToObject(end, "date", civil_date_str(d.times.endDayExcl).c_str());
            if (forPatch)
            {
                // A PATCH switching representations must null the unused member.
                cJSON_AddNullToObject(start, "dateTime");
                cJSON_AddNullToObject(end, "dateTime");
            }
        }
        else
        {
            cJSON_AddStringToObject(start, "dateTime",
                                    rfc3339_utc((uint64_t)d.times.startEpoch).c_str());
            cJSON_AddStringToObject(end, "dateTime",
                                    rfc3339_utc((uint64_t)d.times.endEpoch).c_str());
            if (forPatch)
            {
                cJSON_AddNullToObject(start, "date");
                cJSON_AddNullToObject(end, "date");
            }
        }
    }

    char *printed = cJSON_PrintUnformatted(j);
    std::string out = printed ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(j);
    return out;
}

fujiError_t NetworkProtocolGCAL::event_create(const std::string &selector,
                                              const CalendarEventDraft &d)
{
    const std::string sel = selector_decoded();
    std::string calId;
    if (sel == "*")
    {
        // "Every calendar" cannot be a compose target.
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return FUJI_ERROR::UNSPECIFIED;
    }
    if (sel.empty())
        calId = "primary";
    else
    {
        std::vector<CalendarListEntry> cals;
        resolve_calendars(selector, cals);
        if (cals.empty())
        {
            error = NDEV_STATUS::FILE_NOT_FOUND;
            return FUJI_ERROR::UNSPECIFIED;
        }
        calId = cals[0].id;
    }

    api_send(false, std::string(GCAL_BASE) + "/calendars/" + url_encode(calId) + "/events",
             build_event_json(d, false), "application/json");
    if (_last_http < 200 || _last_http >= 300) return FUJI_ERROR::UNSPECIFIED;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGCAL::event_update(const CalendarEventEntry &ev,
                                              const CalendarEventDraft &d)
{
    // Both providerId halves are percent-encoded already; use them verbatim.
    size_t slash = ev.providerId.find('/');
    if (slash == std::string::npos)
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }
    const std::string calId = ev.providerId.substr(0, slash);
    const std::string evId = ev.providerId.substr(slash + 1);

    api_send(true, std::string(GCAL_BASE) + "/calendars/" + calId + "/events/" + evId,
             build_event_json(d, true), "application/json");
    if (_last_http < 200 || _last_http >= 300) return FUJI_ERROR::UNSPECIFIED;
    return FUJI_ERROR::NONE;
}
