#include "calendar_draft.h"

#include <cctype>
#include <vector>

using namespace fn_time;

namespace
{

std::string trim(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::string upper(std::string s)
{
    for (auto &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// Split on 0x9B / CR / LF / CRLF so every platform's native EOL works.
std::vector<std::string> split_lines(const std::string &raw)
{
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < raw.size(); i++)
    {
        unsigned char c = (unsigned char)raw[i];
        if (c == 0x9B || c == '\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else if (c == '\r')
        {
            if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
            lines.push_back(cur);
            cur.clear();
        }
        else
            cur += raw[i];
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

int64_t day_of(const ParsedTime &p)
{
    return days_from_civil(p.year, p.month, p.day);
}

// Fill times from a validated all-day [startDay, endDayExcl) range.
void fill_allday(CalendarDraftTimes &t, const PosixTz &tz, int64_t startDay, int64_t endDayExcl)
{
    t.allDay = true;
    t.startDay = startDay;
    t.endDayExcl = endDayExcl;
    t.startEpoch = tz.from_local_days(startDay, 0, 0, 0);
    t.endEpoch = tz.from_local_days(endDayExcl, 0, 0, 0);
}

void fill_timed(CalendarDraftTimes &t, const PosixTz &tz, int64_t startEpoch, int64_t endEpoch)
{
    t.allDay = false;
    t.startEpoch = startEpoch;
    t.endEpoch = endEpoch;
    t.startDay = tz.local_day(startEpoch);
    t.endDayExcl = tz.local_day(endEpoch);
}

// START and END both present: shared by compose and edit.
CalDraftError times_from_pair(const CalendarEventDraft &d, const PosixTz &tz, CalendarDraftTimes &t)
{
    if (d.start.dateOnly != d.end.dateOnly)
        return CalDraftError::MIXED_FORMS;

    if (d.start.dateOnly)
    {
        // The written END names the last covered day; Google's is exclusive.
        int64_t s = day_of(d.start);
        int64_t e = day_of(d.end) + 1;
        if (e <= s) return CalDraftError::END_BEFORE_START;
        fill_allday(t, tz, s, e);
    }
    else
    {
        int64_t s = resolve(d.start, tz);
        int64_t e = resolve(d.end, tz);
        if (e <= s) return CalDraftError::END_BEFORE_START;
        fill_timed(t, tz, s, e);
    }
    return CalDraftError::NONE;
}

// START alone: compose defaults of one hour / one day.
void times_from_start(const CalendarEventDraft &d, const PosixTz &tz, CalendarDraftTimes &t)
{
    if (d.start.dateOnly)
    {
        int64_t s = day_of(d.start);
        fill_allday(t, tz, s, s + 1);
    }
    else
    {
        int64_t s = resolve(d.start, tz);
        fill_timed(t, tz, s, s + 3600);
    }
}

} // namespace

CalDraftError cal_draft_parse(const std::string &raw, CalendarEventDraft &out)
{
    for (auto &line : split_lines(raw))
    {
        std::string l = trim(line);
        if (l.empty()) continue;

        size_t colon = l.find(':');
        if (colon == std::string::npos)
            return CalDraftError::BAD_LINE;

        std::string key = upper(trim(l.substr(0, colon)));
        std::string value = trim(l.substr(colon + 1));

        if (key == "SUMMARY")
        {
            out.summary = value;
            out.has_summary = true;
        }
        else if (key == "START")
        {
            if (!parse_datetime(value, out.start))
                return CalDraftError::BAD_TIME;
            out.has_start = true;
        }
        else if (key == "END")
        {
            if (!parse_datetime(value, out.end))
                return CalDraftError::BAD_TIME;
            out.has_end = true;
        }
        else if (key == "LOCATION")
        {
            out.location = value;
            out.has_location = true;
        }
        else if (key == "DESCRIPTION")
        {
            if (out.has_description)
                out.description += '\n';
            out.description += value;
            out.has_description = true;
        }
        else if (key == "CATEGORY")
        {
            out.category = value;
            out.has_category = true;
        }
        else
            return CalDraftError::BAD_KEY;
    }
    return CalDraftError::NONE;
}

CalDraftError cal_draft_finalize(CalendarEventDraft &d, const PosixTz &tz,
                                 const CalendarDraftTimes *existing)
{
    if (!d.has_summary && !d.has_start && !d.has_end && !d.has_location &&
        !d.has_description && !d.has_category)
        return CalDraftError::EMPTY;

    d.timesValid = false;

    if (existing == nullptr)
    {
        if (!d.has_summary) return CalDraftError::MISSING_SUMMARY;
        if (!d.has_start) return CalDraftError::MISSING_START;

        if (d.has_end)
        {
            CalDraftError r = times_from_pair(d, tz, d.times);
            if (r != CalDraftError::NONE) return r;
        }
        else
            times_from_start(d, tz, d.times);
        d.timesValid = true;
        return CalDraftError::NONE;
    }

    // Edit: no time given means no time changes.
    if (!d.has_start && !d.has_end)
        return CalDraftError::NONE;

    if (d.has_start && d.has_end)
    {
        CalDraftError r = times_from_pair(d, tz, d.times);
        if (r != CalDraftError::NONE) return r;
    }
    else if (d.has_start)
    {
        bool newAllDay = d.start.dateOnly;
        if (newAllDay != existing->allDay)
        {
            // Duration is meaningless across a form change; use compose defaults.
            times_from_start(d, tz, d.times);
        }
        else if (newAllDay)
        {
            int64_t span = existing->endDayExcl - existing->startDay;
            if (span < 1) span = 1;
            int64_t s = day_of(d.start);
            fill_allday(d.times, tz, s, s + span);
        }
        else
        {
            int64_t dur = existing->endEpoch - existing->startEpoch;
            if (dur < 1) dur = 3600;
            int64_t s = resolve(d.start, tz);
            fill_timed(d.times, tz, s, s + dur);
        }
    }
    else // END alone
    {
        if (d.end.dateOnly != existing->allDay)
            return CalDraftError::MIXED_FORMS;

        if (existing->allDay)
        {
            int64_t e = day_of(d.end) + 1;
            if (e <= existing->startDay) return CalDraftError::END_BEFORE_START;
            fill_allday(d.times, tz, existing->startDay, e);
        }
        else
        {
            int64_t e = resolve(d.end, tz);
            if (e <= existing->startEpoch) return CalDraftError::END_BEFORE_START;
            fill_timed(d.times, tz, existing->startEpoch, e);
        }
    }

    d.timesValid = true;
    return CalDraftError::NONE;
}
