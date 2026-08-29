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
