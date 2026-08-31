/**
 * calendar_draft - parsing and validation for composed/edited calendar events.
 *
 * The host writes an event as text field lines ("SUMMARY: Lunch", "START:
 * 2026-09-01 12:00", ...) and CLOSE commits it. This unit turns that text into
 * a validated draft. It depends only on fn_time and the standard library so the
 * calendar_tests target can link it without pulling in Protocol.h -> bus.h.
 */

#ifndef CALENDAR_DRAFT_H
#define CALENDAR_DRAFT_H

#include <cstdint>
#include <string>

#include "../utils/fn_time.h"

/**
 * Finalized event times. For all-day events the civil day numbers are
 * authoritative (endDayExcl is exclusive, Google-style); the epochs are the
 * corresponding local midnights. For timed events the epochs are authoritative.
 */
struct CalendarDraftTimes
{
    bool allDay = false;
    int64_t startEpoch = 0; // UTC epoch seconds
    int64_t endEpoch = 0;   // exclusive
    int64_t startDay = 0;   // days since 1970-01-01, local civil date
    int64_t endDayExcl = 0; // exclusive
};

struct CalendarEventDraft
{
    bool has_summary = false;
    bool has_start = false;
    bool has_end = false;
    bool has_location = false;
    bool has_description = false;
    bool has_category = false;

    std::string summary;
    std::string location;
    std::string description;
    std::string category;

    fn_time::ParsedTime start;
    fn_time::ParsedTime end;

    // Set by cal_draft_finalize when the draft carries time information.
    // A finalized edit draft with timesValid == false changes no times.
    bool timesValid = false;
    CalendarDraftTimes times;
};

enum class CalDraftError
{
    NONE,
    EMPTY,            // no recognized field in the draft
    BAD_LINE,         // non-blank line without "KEY: value" shape
    BAD_KEY,          // unknown key (typos must not silently drop data)
    BAD_TIME,         // START/END value did not parse
    MISSING_SUMMARY,  // compose requires SUMMARY
    MISSING_START,    // compose requires START
    END_BEFORE_START,
    MIXED_FORMS,      // date-only and date-time forms mixed incompatibly
};

/**
 * Parse field lines into a draft. Lines end with any of 0x9B (ATASCII), CR,
 * LF or CRLF. Keys are case-insensitive; values are trimmed. Duplicate keys
 * are last-wins, except DESCRIPTION, which appends with '\n'.
 */
CalDraftError cal_draft_parse(const std::string &raw, CalendarEventDraft &out);

/**
 * Validate the draft and fill in its finalized times.
 *
 * existing == nullptr means compose: SUMMARY and START are required, a
 * date-only START makes an all-day event, and a missing END defaults to one
 * hour (timed) or one day (all-day). In the field format an all-day END names
 * the last covered day (inclusive); it is converted to the exclusive form here.
 *
 * existing != nullptr means edit: at least one field is required and only the
 * given ones change. A START-only edit preserves the event's duration; an
 * END-only edit keeps the start. Changing between all-day and timed forms
 * needs a START (END alone cannot switch forms).
 */
CalDraftError cal_draft_finalize(CalendarEventDraft &d, const fn_time::PosixTz &tz,
                                 const CalendarDraftTimes *existing);

#endif /* CALENDAR_DRAFT_H */
