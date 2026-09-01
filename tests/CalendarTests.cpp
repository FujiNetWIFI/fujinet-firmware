#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "utils/fn_time.h"

using namespace fn_time;

// ─── civil calendar ───────────────────────────────────────────────────────────

TEST_CASE("civil date round-trips")
{
    for (int64_t z = -50000; z < 50000; z += 3)
    {
        int y;
        unsigned m, d;
        civil_from_days(z, y, m, d);
        REQUIRE(days_from_civil(y, m, d) == z);
    }
}

TEST_CASE("civil date anchors")
{
    CHECK(days_from_civil(1970, 1, 1) == 0);
    CHECK(days_from_civil(1969, 12, 31) == -1);
    CHECK(days_from_civil(2000, 3, 1) == 11017);
    CHECK(weekday_from_days(0) == 4); // 1970-01-01 was a Thursday

    int y;
    unsigned m, d;
    civil_from_days(0, y, m, d);
    CHECK(y == 1970);
    CHECK(m == 1);
    CHECK(d == 1);
}

TEST_CASE("leap years and month lengths")
{
    CHECK(is_leap(2024));
    CHECK_FALSE(is_leap(2025));
    CHECK_FALSE(is_leap(1900));
    CHECK(is_leap(2000));

    CHECK(days_in_month(2024, 2) == 29);
    CHECK(days_in_month(2025, 2) == 28);
    CHECK(days_in_month(2026, 1) == 31);
    CHECK(days_in_month(2026, 4) == 30);

    // February 29 exists and round-trips.
    int64_t z = days_from_civil(2024, 2, 29);
    int y;
    unsigned m, d;
    civil_from_days(z, y, m, d);
    CHECK(y == 2024);
    CHECK(m == 2);
    CHECK(d == 29);
}

TEST_CASE("fn_timegm replaces the missing timegm")
{
    CHECK(fn_timegm(1970, 1, 1, 0, 0, 0) == 0);
    CHECK(fn_timegm(1970, 1, 2, 0, 0, 0) == 86400);
    CHECK(fn_timegm(2000, 1, 1, 0, 0, 0) == 946684800);
    CHECK(fn_timegm(2026, 8, 28, 12, 34, 56) ==
          days_from_civil(2026, 8, 28) * 86400 + 12 * 3600 + 34 * 60 + 56);
}

// ─── POSIX TZ parsing ─────────────────────────────────────────────────────────

TEST_CASE("POSIX TZ parsing")
{
    SUBCASE("fixed offset, no DST")
    {
        PosixTz z;
        REQUIRE(z.parse("MST+7"));
        CHECK(z.stdOff == -7 * 3600); // POSIX writes west-positive; we store east
        CHECK_FALSE(z.hasDst);
    }

    SUBCASE("east of UTC")
    {
        PosixTz z;
        REQUIRE(z.parse("FNTZ-10"));
        CHECK(z.stdOff == 10 * 3600);
        CHECK_FALSE(z.hasDst);
    }

    SUBCASE("DST abbreviation with no rules assumes the US rules")
    {
        PosixTz z;
        REQUIRE(z.parse("CST+6CDT"));
        CHECK(z.stdOff == -6 * 3600);
        CHECK(z.dstOff == -5 * 3600);
        CHECK(z.hasDst);
    }

    SUBCASE("explicit rules")
    {
        PosixTz z;
        REQUIRE(z.parse("CET-1CEST,M3.5.0,M10.5.0/3"));
        CHECK(z.stdOff == 3600);
        CHECK(z.dstOff == 7200);
        CHECK(z.hasDst);
    }

    SUBCASE("an IANA name has no tz database behind it and is refused")
    {
        PosixTz z;
        CHECK_FALSE(z.parse("America/Chicago"));
        CHECK_FALSE(z.parse(":America/Chicago"));
        // A rule's "/hh" transition time must not be mistaken for an IANA slash.
        CHECK(z.parse("GMT+0BST,M3.5.0/1,M10.5.0"));
    }

    SUBCASE("empty and garbage fall back rather than throwing")
    {
        PosixTz z;
        CHECK_FALSE(z.parse(""));
        CHECK(z.stdOff == 0);
        CHECK_FALSE(z.hasDst);
    }
}

// ─── DST transitions ──────────────────────────────────────────────────────────

TEST_CASE("US DST transitions, derived rather than hard-coded")
{
    PosixTz z;
    REQUIRE(z.parse("CST+6CDT")); // America/Chicago, as the web UI spells it

    SUBCASE("standard and daylight offsets apply in the right seasons")
    {
        CHECK(z.offset_at(fn_timegm(2026, 1, 15, 18, 0, 0)) == -6 * 3600);
        CHECK(z.offset_at(fn_timegm(2026, 7, 15, 17, 0, 0)) == -5 * 3600);
    }

    SUBCASE("spring forward: 01:59:59 and 03:00:00 are one second apart")
    {
        // 02:00-02:59 local never happens on 2026-03-08.
        int64_t before = z.from_local(2026, 3, 8, 1, 59, 59);
        int64_t after = z.from_local(2026, 3, 8, 3, 0, 0);
        CHECK(after - before == 1);
    }

    SUBCASE("a time inside the spring-forward gap resolves forward, not to -1")
    {
        int64_t gap = z.from_local(2026, 3, 8, 2, 30, 0);
        CHECK(gap == fn_timegm(2026, 3, 8, 8, 30, 0));
    }

    SUBCASE("a time inside the fall-back overlap resolves to the earlier instant")
    {
        // 01:30 local happens twice on 2026-11-01; the first is still CDT.
        int64_t amb = z.from_local(2026, 11, 1, 1, 30, 0);
        CHECK(amb == fn_timegm(2026, 11, 1, 6, 30, 0));
        CHECK(z.offset_at(amb) == -5 * 3600);
    }
}

TEST_CASE("a DST day is not 86400 seconds long")
{
    // This is the reason window edges are always re-derived from civil dates
    // instead of being computed as start + 86400.
    SUBCASE("northern hemisphere")
    {
        PosixTz z;
        REQUIRE(z.parse("CST+6CDT"));

        int64_t spring = days_from_civil(2026, 3, 8);
        CHECK(z.from_local_days(spring + 1, 0, 0, 0) - z.from_local_days(spring, 0, 0, 0) == 82800);

        int64_t fall = days_from_civil(2026, 11, 1);
        CHECK(z.from_local_days(fall + 1, 0, 0, 0) - z.from_local_days(fall, 0, 0, 0) == 90000);

        int64_t plain = days_from_civil(2026, 6, 1);
        CHECK(z.from_local_days(plain + 1, 0, 0, 0) - z.from_local_days(plain, 0, 0, 0) == 86400);
    }

    SUBCASE("europe, with explicit last-Sunday rules")
    {
        PosixTz z;
        REQUIRE(z.parse("CET-1CEST,M3.5.0,M10.5.0/3"));

        int64_t spring = days_from_civil(2026, 3, 29); // last Sunday in March
        CHECK(z.from_local_days(spring + 1, 0, 0, 0) - z.from_local_days(spring, 0, 0, 0) == 82800);

        int64_t fall = days_from_civil(2026, 10, 25); // last Sunday in October
        CHECK(z.from_local_days(fall + 1, 0, 0, 0) - z.from_local_days(fall, 0, 0, 0) == 90000);
    }

    SUBCASE("southern hemisphere, where DST spans the new year")
    {
        PosixTz z;
        REQUIRE(z.parse("AEST-10AEDT,M10.1.0,M4.1.0/3"));

        // January is inside DST below the equator.
        CHECK(z.offset_at(fn_timegm(2026, 1, 15, 0, 0, 0)) == 11 * 3600);
        CHECK(z.offset_at(fn_timegm(2026, 7, 15, 0, 0, 0)) == 10 * 3600);

        int64_t forward = days_from_civil(2026, 10, 4); // first Sunday in October
        CHECK(z.from_local_days(forward + 1, 0, 0, 0) - z.from_local_days(forward, 0, 0, 0) == 82800);

        int64_t back = days_from_civil(2026, 4, 5); // first Sunday in April
        CHECK(z.from_local_days(back + 1, 0, 0, 0) - z.from_local_days(back, 0, 0, 0) == 90000);
    }
}

TEST_CASE("local_day and to_local agree with from_local_days")
{
    PosixTz z;
    REQUIRE(z.parse("CST+6CDT"));

    int64_t day = days_from_civil(2026, 8, 28);
    int64_t midnight = z.from_local_days(day, 0, 0, 0);

    CHECK(z.local_day(midnight) == day);
    CHECK(z.local_day(midnight + 86399) == day);

    int y, h, mi, s, wd;
    unsigned mo, d;
    z.to_local(midnight, y, mo, d, h, mi, s, wd);
    CHECK(y == 2026);
    CHECK(mo == 8);
    CHECK(d == 28);
    CHECK(h == 0);
    CHECK(mi == 0);
    CHECK(wd == 5); // 2026-08-28 is a Friday
}

// ─── date/time parsing ────────────────────────────────────────────────────────

TEST_CASE("ISO 8601 and iCalendar date-time parsing")
{
    ParsedTime p;

    SUBCASE("plain dates, dashed and basic")
    {
        REQUIRE(parse_datetime("2026-08-28", p));
        CHECK(p.year == 2026);
        CHECK(p.month == 8);
        CHECK(p.day == 28);
        CHECK(p.dateOnly);
        CHECK_FALSE(p.utc);

        REQUIRE(parse_datetime("20260828", p));
        CHECK(p.day == 28);
        CHECK(p.dateOnly);
    }

    SUBCASE("iCalendar UTC date-time")
    {
        REQUIRE(parse_datetime("20260828T193000Z", p));
        CHECK(p.hour == 19);
        CHECK(p.minute == 30);
        CHECK(p.second == 0);
        CHECK(p.utc);
        CHECK_FALSE(p.dateOnly);
    }

    SUBCASE("iCalendar floating date-time")
    {
        REQUIRE(parse_datetime("20260828T143000", p));
        CHECK(p.hour == 14);
        CHECK_FALSE(p.utc);
        CHECK_FALSE(p.hasOffset);
    }

    SUBCASE("RFC 3339 with a numeric offset")
    {
        REQUIRE(parse_datetime("2026-08-28T14:30:00-05:00", p));
        CHECK(p.hasOffset);
        CHECK(p.offset == -5 * 3600);

        REQUIRE(parse_datetime("2026-08-28T14:30:00+0530", p));
        CHECK(p.offset == 5 * 3600 + 1800);
    }

    SUBCASE("fractional seconds are accepted and discarded")
    {
        REQUIRE(parse_datetime("2026-08-28T14:30:00.123456Z", p));
        CHECK(p.second == 0);
        CHECK(p.utc);
    }

    SUBCASE("a space may stand in for the T, and seconds may be absent")
    {
        REQUIRE(parse_datetime("2026-08-28 14:30", p));
        CHECK(p.hour == 14);
        CHECK(p.minute == 30);
    }

    SUBCASE("malformed input is rejected rather than silently accepted")
    {
        CHECK_FALSE(parse_datetime("", p));
        CHECK_FALSE(parse_datetime("garbage", p));
        CHECK_FALSE(parse_datetime("2026-13-01", p));  // month 13
        CHECK_FALSE(parse_datetime("2026-00-01", p));  // month 0
        CHECK_FALSE(parse_datetime("2026-08-32", p));  // day 32
        CHECK_FALSE(parse_datetime("2026-08", p));     // that is a year-month
        CHECK_FALSE(parse_datetime("2026-08-28T14", p));
        CHECK_FALSE(parse_datetime("2026-08-28T14:30:00Zjunk", p));
    }
}

TEST_CASE("year-month parsing, for the MONTH view")
{
    ParsedTime p;

    REQUIRE(parse_yearmonth("2026-08", p));
    CHECK(p.year == 2026);
    CHECK(p.month == 8);
    CHECK(p.day == 1);
    CHECK(p.dateOnly);

    REQUIRE(parse_yearmonth("202608", p));
    CHECK(p.month == 8);

    CHECK_FALSE(parse_yearmonth("2026-13", p));
    CHECK_FALSE(parse_yearmonth("2026-08-28", p)); // a full date is not a year-month
    CHECK_FALSE(parse_yearmonth("", p));
}

TEST_CASE("resolve honours Z, explicit offsets, and floating local time")
{
    PosixTz z;
    REQUIRE(z.parse("CST+6CDT"));

    ParsedTime p;

    SUBCASE("a Z-suffixed time is absolute")
    {
        REQUIRE(parse_datetime("20260828T190000Z", p));
        CHECK(resolve(p, z) == fn_timegm(2026, 8, 28, 19, 0, 0));
    }

    SUBCASE("an explicit offset is absolute")
    {
        REQUIRE(parse_datetime("2026-08-28T14:00:00-05:00", p));
        CHECK(resolve(p, z) == fn_timegm(2026, 8, 28, 19, 0, 0));
    }

    SUBCASE("a floating time is interpreted in the configured zone")
    {
        REQUIRE(parse_datetime("20260828T140000", p));
        CHECK(resolve(p, z) == fn_timegm(2026, 8, 28, 19, 0, 0)); // August is CDT, UTC-5
    }

    SUBCASE("an all-day date anchors to LOCAL midnight, not UTC midnight")
    {
        // Anchoring to UTC midnight would show the event on the previous day for
        // anyone west of Greenwich.
        REQUIRE(parse_datetime("2026-08-28", p));
        int64_t t = resolve(p, z);
        CHECK(t == fn_timegm(2026, 8, 28, 5, 0, 0)); // 00:00 CDT
        CHECK(z.local_day(t) == days_from_civil(2026, 8, 28));
    }
}

// ─── event draft parsing (compose / edit field lines) ─────────────────────────

#include "network-protocol/calendar_draft.h"

TEST_CASE("draft parsing accepts every platform EOL")
{
    const char *eols[] = {"\n", "\r\n", "\r", "\x9b"};
    for (const char *eol : eols)
    {
        std::string raw = std::string("SUMMARY: Lunch") + eol + "START: 2026-09-01 12:00" + eol +
                          "LOCATION: Cafe" + eol;
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse(raw, d) == CalDraftError::NONE);
        CHECK(d.summary == "Lunch");
        CHECK(d.location == "Cafe");
        CHECK(d.has_start);
        CHECK(d.start.hour == 12);
    }

    // Mixed EOLs in one draft still parse.
    CalendarEventDraft d;
    REQUIRE(cal_draft_parse("SUMMARY: A\r\nSTART: 2026-09-01\x9bLOCATION: B\n", d) ==
            CalDraftError::NONE);
    CHECK(d.summary == "A");
    CHECK(d.location == "B");
}

TEST_CASE("draft parsing keys and values")
{
    SUBCASE("keys are case-insensitive and whitespace is trimmed")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("summary :  Dentist  \nStArT: 2026-09-01\n", d) ==
                CalDraftError::NONE);
        CHECK(d.summary == "Dentist");
        CHECK(d.has_start);
    }

    SUBCASE("DESCRIPTION accumulates, everything else is last-wins")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: one\nSUMMARY: two\n"
                                "DESCRIPTION: line1\nDESCRIPTION: line2\n",
                                d) == CalDraftError::NONE);
        CHECK(d.summary == "two");
        CHECK(d.description == "line1\nline2");
    }

    SUBCASE("blank lines are skipped")
    {
        CalendarEventDraft d;
        CHECK(cal_draft_parse("\n  \nSUMMARY: x\n\n", d) == CalDraftError::NONE);
        CHECK(d.has_summary);
    }

    SUBCASE("errors")
    {
        CalendarEventDraft d;
        CHECK(cal_draft_parse("SUMARY: typo\n", d) == CalDraftError::BAD_KEY);
        CHECK(cal_draft_parse("no colon here\n", d) == CalDraftError::BAD_LINE);
        CHECK(cal_draft_parse("START: not-a-date\n", d) == CalDraftError::BAD_TIME);
    }
}

TEST_CASE("draft finalize: compose")
{
    PosixTz z;
    REQUIRE(z.parse("CST+6CDT,M3.2.0/2,M11.1.0/2"));

    SUBCASE("timed default END is one hour")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-01 14:00\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, nullptr) == CalDraftError::NONE);
        CHECK(d.timesValid);
        CHECK_FALSE(d.times.allDay);
        CHECK(d.times.startEpoch == fn_timegm(2026, 9, 1, 19, 0, 0)); // CDT, UTC-5
        CHECK(d.times.endEpoch == d.times.startEpoch + 3600);
    }

    SUBCASE("date-only START makes a one-day all-day event")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-01\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, nullptr) == CalDraftError::NONE);
        CHECK(d.times.allDay);
        CHECK(d.times.startDay == days_from_civil(2026, 9, 1));
        CHECK(d.times.endDayExcl == d.times.startDay + 1);
        CHECK(d.times.startEpoch == fn_timegm(2026, 9, 1, 5, 0, 0)); // local midnight
    }

    SUBCASE("all-day END names the last covered day, inclusive")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-01\nEND: 2026-09-02\n", d) ==
                CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, nullptr) == CalDraftError::NONE);
        CHECK(d.times.endDayExcl == days_from_civil(2026, 9, 3));
    }

    SUBCASE("an all-day span across spring-forward is not a multiple of 86400")
    {
        // 2026-03-08 02:00 is the US transition; the two-day event 03-07..03-08
        // is 47 wall-clock hours.
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-03-07\nEND: 2026-03-08\n", d) ==
                CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, nullptr) == CalDraftError::NONE);
        CHECK(d.times.endEpoch - d.times.startEpoch == 2 * 86400 - 3600);
    }

    SUBCASE("an explicit Z resolves absolutely")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-01T14:00Z\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, nullptr) == CalDraftError::NONE);
        CHECK(d.times.startEpoch == fn_timegm(2026, 9, 1, 14, 0, 0));
    }

    SUBCASE("rejections")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("", d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, nullptr) == CalDraftError::EMPTY);

        d = {};
        REQUIRE(cal_draft_parse("START: 2026-09-01\n", d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, nullptr) == CalDraftError::MISSING_SUMMARY);

        d = {};
        REQUIRE(cal_draft_parse("SUMMARY: x\n", d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, nullptr) == CalDraftError::MISSING_START);

        d = {};
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-01 15:00\nEND: 2026-09-01 14:00\n",
                                d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, nullptr) == CalDraftError::END_BEFORE_START);

        d = {};
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-02\nEND: 2026-09-01\n", d) ==
                CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, nullptr) == CalDraftError::END_BEFORE_START);

        d = {};
        REQUIRE(cal_draft_parse("SUMMARY: x\nSTART: 2026-09-01 14:00\nEND: 2026-09-02\n", d) ==
                CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, nullptr) == CalDraftError::MIXED_FORMS);
    }
}

TEST_CASE("draft finalize: edit")
{
    PosixTz z;
    REQUIRE(z.parse("CST+6CDT,M3.2.0/2,M11.1.0/2"));

    // A 90-minute timed event on 2026-09-01, 14:00 local.
    CalendarDraftTimes timed;
    timed.allDay = false;
    timed.startEpoch = fn_timegm(2026, 9, 1, 19, 0, 0);
    timed.endEpoch = timed.startEpoch + 5400;
    timed.startDay = days_from_civil(2026, 9, 1);
    timed.endDayExcl = timed.startDay;

    // A two-day all-day event 2026-09-01..09-02.
    CalendarDraftTimes allday;
    allday.allDay = true;
    allday.startDay = days_from_civil(2026, 9, 1);
    allday.endDayExcl = allday.startDay + 2;
    allday.startEpoch = z.from_local_days(allday.startDay, 0, 0, 0);
    allday.endEpoch = z.from_local_days(allday.endDayExcl, 0, 0, 0);

    SUBCASE("START alone preserves the duration")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("START: 2026-09-02 09:00\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, &timed) == CalDraftError::NONE);
        CHECK(d.times.startEpoch == fn_timegm(2026, 9, 2, 14, 0, 0));
        CHECK(d.times.endEpoch == d.times.startEpoch + 5400);
    }

    SUBCASE("START alone preserves an all-day span")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("START: 2026-09-10\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, &allday) == CalDraftError::NONE);
        CHECK(d.times.allDay);
        CHECK(d.times.startDay == days_from_civil(2026, 9, 10));
        CHECK(d.times.endDayExcl == d.times.startDay + 2);
    }

    SUBCASE("a form-changing START falls back to compose defaults")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("START: 2026-09-10\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, &timed) == CalDraftError::NONE);
        CHECK(d.times.allDay);
        CHECK(d.times.endDayExcl == d.times.startDay + 1);
    }

    SUBCASE("END alone keeps the start")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("END: 2026-09-01 16:00\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, &timed) == CalDraftError::NONE);
        CHECK(d.times.startEpoch == timed.startEpoch);
        CHECK(d.times.endEpoch == fn_timegm(2026, 9, 1, 21, 0, 0));
    }

    SUBCASE("END alone cannot precede the start or change forms")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("END: 2026-09-01 13:00\n", d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, &timed) == CalDraftError::END_BEFORE_START);

        d = {};
        REQUIRE(cal_draft_parse("END: 2026-09-03\n", d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, &timed) == CalDraftError::MIXED_FORMS);
    }

    SUBCASE("a time-less edit leaves the times alone")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("SUMMARY: renamed\n", d) == CalDraftError::NONE);
        REQUIRE(cal_draft_finalize(d, z, &timed) == CalDraftError::NONE);
        CHECK_FALSE(d.timesValid);
    }

    SUBCASE("an empty edit is EMPTY")
    {
        CalendarEventDraft d;
        REQUIRE(cal_draft_parse("", d) == CalDraftError::NONE);
        CHECK(cal_draft_finalize(d, z, &timed) == CalDraftError::EMPTY);
    }
}
