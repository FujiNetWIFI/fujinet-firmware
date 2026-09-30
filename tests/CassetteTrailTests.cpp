#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "device/sio/cassetteTrail.h"

// The trail is fed the way sio_handle_cassette() feeds it: sync() before the tape moves, played() for
// each record that completes. Tapes here are made of records of known lengths, so every landing can be
// worked out by hand.

namespace
{
struct Record
{
    size_t offset; // where the tape is after the record
    uint32_t ms;
    unsigned short baud; // in effect after it
};

// A, B, C, D: 10 s each, at offsets 100, 200, 300, 400
const std::vector<Record> ABCD = {{100, 10000, 600}, {200, 10000, 600}, {300, 10000, 600}, {400, 10000, 600}};

constexpr uint32_t TAPE = 1;

void play(CassetteTrail &t, const std::vector<Record> &records)
{
    for (const Record &r : records)
    {
        t.sync(TAPE, t.cursor().offset);
        t.played(r.offset, r.ms, r.baud);
    }
}

void start(CassetteTrail &t)
{
    t.sync(TAPE, 0);
}

void expect_boundary(const CassetteBoundary &b, uint32_t offset, uint32_t time_ms, unsigned short baud)
{
    CHECK(b.offset == offset);
    CHECK(b.time_ms == time_ms);
    CHECK(b.baud == baud);
}

// allocator that is told when to fail
int alloc_calls = 0;
size_t alloc_bytes = 0;
bool alloc_fails = false;

void *test_alloc(size_t bytes)
{
    ++alloc_calls;
    alloc_bytes = bytes;
    return alloc_fails ? nullptr : std::malloc(bytes);
}
} // namespace

TEST_CASE("a boundary is 12 bytes")
{
    CHECK(sizeof(CassetteBoundary) == 12);
}

TEST_CASE("a new trail starts at the start of the tape")
{
    CassetteTrail t;
    start(t);
    CHECK(t.valid());
    CHECK(t.size() == 1);
    expect_boundary(t.cursor(), 0, 0, 600);
    expect_boundary(t.rewind_by(5), 0, 0, 600);
}

TEST_CASE("played records extend the trail")
{
    CassetteTrail t;
    start(t);
    play(t, {ABCD[0], ABCD[1]});

    CHECK(t.size() == 3);
    expect_boundary(t.at(1), 100, 10000, 600);
    expect_boundary(t.at(2), 200, 20000, 600);
    expect_boundary(t.cursor(), 200, 20000, 600);
}

TEST_CASE("a rewind lands on the last boundary at or before the point, never after it")
{
    struct Case
    {
        uint32_t back_s;
        uint32_t offset;
        uint32_t time_ms;
    };
    // the tape is at D, 40 s
    const Case cases[] = {
        {1, 300, 30000},  // 39 s: inside D, so the start of D
        {10, 300, 30000}, // exactly 30 s
        {11, 200, 20000}, // 29 s
        {15, 200, 20000}, // 25 s
        {20, 200, 20000}, // exactly 20 s
        {30, 100, 10000}, // exactly 10 s
        {31, 0, 0},       // 9 s: before the first record, so the start
        {39, 0, 0},
    };
    for (const Case &c : cases)
    {
        CAPTURE(c.back_s);
        CassetteTrail t;
        start(t);
        play(t, ABCD);
        expect_boundary(t.rewind_by(c.back_s), c.offset, c.time_ms, 600);
    }
}

TEST_CASE("a rewind farther than the tape has played is the start")
{
    for (uint32_t back_s : {40u, 41u, 4000u, UINT32_MAX})
    {
        CAPTURE(back_s);
        CassetteTrail t;
        start(t);
        play(t, ABCD);
        expect_boundary(t.rewind_by(back_s), 0, 0, 600);
        CHECK(t.size() == 1);
    }
}

TEST_CASE("a rewind moves the active position back and forgets what came after")
{
    CassetteTrail t;
    start(t);
    play(t, ABCD);

    expect_boundary(t.rewind_by(15), 200, 20000, 600); // B
    CHECK(t.size() == 3);                              // start, A, B
    expect_boundary(t.cursor(), 200, 20000, 600);
}

TEST_CASE("after a rewind only the history played since is used")
{
    CassetteTrail t;
    start(t);
    play(t, ABCD);

    // back to B, and play on differently from there: C' is shorter and elsewhere, then D'
    expect_boundary(t.rewind_by(15), 200, 20000, 600);
    play(t, {{350, 5000, 600}, {450, 10000, 600}}); // C' at 25 s, D' at 35 s
    CHECK(t.size() == 5);

    // 8 s back from D' is 27 s: C', not the old C (30 s) or D (40 s) from before the rewind
    expect_boundary(t.rewind_by(8), 350, 25000, 600);
    CHECK(t.size() == 4);

    // and again, from C': 6 s back is 19 s, which is A
    expect_boundary(t.rewind_by(6), 100, 10000, 600);
    CHECK(t.size() == 2);

    // and once more forward
    play(t, {{210, 10000, 600}});
    expect_boundary(t.rewind_by(1), 100, 10000, 600);
}

TEST_CASE("a rewind to the start begins the trail again")
{
    CassetteTrail t;
    start(t);
    play(t, ABCD);

    t.restart();
    CHECK(t.valid());
    CHECK(t.size() == 1);
    CHECK(t.stride() == 1);
    expect_boundary(t.cursor(), 0, 0, 600);
    expect_boundary(t.rewind_by(5), 0, 0, 600);

    play(t, {ABCD[0]});
    CHECK(t.size() == 2);
}

TEST_CASE("a trail belongs to one tape")
{
    CassetteTrail t;
    start(t);
    play(t, {ABCD[0], ABCD[1]});

    SUBCASE("the same tape, where the trail says it is, keeps it")
    {
        t.sync(TAPE, 200);
        CHECK(t.size() == 3);
    }
    SUBCASE("another tape starts it over")
    {
        t.sync(TAPE + 1, 200);
        CHECK(t.valid());
        CHECK(t.size() == 1);
        expect_boundary(t.cursor(), 0, 0, 600);
        expect_boundary(t.rewind_by(5), 0, 0, 600); // nothing of the old tape is applied
    }
    SUBCASE("the tape coming back to the start starts it over")
    {
        t.sync(TAPE, 0);
        CHECK(t.size() == 1);
        expect_boundary(t.cursor(), 0, 0, 600);
    }
    SUBCASE("a tape somewhere the trail does not know makes it unusable, and a rewind answers the start")
    {
        t.sync(TAPE, 999);
        CHECK_FALSE(t.valid());
        t.played(1099, 10000, 600); // ignored
        expect_boundary(t.rewind_by(5), 0, 0, 600);
        CHECK(t.valid()); // the tape is at the start again, so is the trail
        CHECK(t.size() == 1);
    }
}

TEST_CASE("the baud of a boundary comes back with it")
{
    CassetteTrail t;
    start(t);
    play(t, {{100, 10000, 600}, {200, 10000, 1200}, {300, 10000, 1200}, {400, 10000, 6580}});

    SUBCASE("D, 40 s")
    {
        expect_boundary(t.rewind_by(10), 300, 30000, 1200);
    }
    SUBCASE("B")
    {
        expect_boundary(t.rewind_by(20), 200, 20000, 1200);
    }
    SUBCASE("A")
    {
        expect_boundary(t.rewind_by(25), 100, 10000, 600);
    }
    SUBCASE("the start is the nominal baud")
    {
        expect_boundary(t.rewind_by(99), 0, 0, 600);
    }
}

TEST_CASE("time and position do not wrap")
{
    CassetteTrail t;
    start(t);
    t.played(100, UINT32_MAX, 600);
    t.played(200, UINT32_MAX, 600);
    CHECK(t.cursor().time_ms == UINT32_MAX);

    if (sizeof(size_t) > sizeof(uint32_t))
    {
        CassetteTrail wide;
        start(wide);
        wide.played(size_t(1) << 33, 10, 600); // an offset a boundary cannot hold
        CHECK_FALSE(wide.valid());
    }
}

TEST_CASE("a full trail keeps thinning itself and stays within its capacity")
{
    constexpr size_t capacity = 8;
    constexpr int records = 100;

    CassetteTrail t(capacity);
    start(t);
    for (int i = 1; i <= records; ++i)
    {
        t.sync(TAPE, t.cursor().offset);
        t.played(size_t(i) * 100, 10000, 600);
        REQUIRE(t.size() <= capacity);
    }
    CHECK(t.stride() > 1);

    // what is kept is real boundaries in order, the start first, with the state they had
    expect_boundary(t.at(0), 0, 0, 600);
    for (size_t i = 1; i < t.size(); ++i)
    {
        CHECK(t.at(i).offset > t.at(i - 1).offset);
        CHECK(t.at(i).offset % 100 == 0);
        CHECK(t.at(i).time_ms == t.at(i).offset / 100 * 10000);
        CHECK(t.at(i).baud == 600);
    }
}

TEST_CASE("thinning only ever lands farther back than the exact point")
{
    constexpr int records = 100; // 1000 s
    bool landed_farther = false;

    for (uint32_t back_s : {1u, 5u, 9u, 10u, 37u, 100u, 333u, 999u})
    {
        CAPTURE(back_s);
        CassetteTrail t(8);
        start(t);
        for (int i = 1; i <= records; ++i)
        {
            t.sync(TAPE, t.cursor().offset);
            t.played(size_t(i) * 100, 10000, 600);
        }

        const uint32_t target_ms = records * 10000 - back_s * 1000;
        const uint32_t exact_ms = target_ms / 10000 * 10000; // the last boundary at or before it

        const CassetteBoundary got = t.rewind_by(back_s);
        CHECK(got.time_ms <= target_ms);
        CHECK(got.time_ms <= exact_ms);
        CHECK(got.time_ms == got.offset / 100 * 10000); // a real boundary
        landed_farther = landed_farther || got.time_ms < exact_ms;
    }
    CHECK(landed_farther);
}

TEST_CASE("a trail with almost no room still works")
{
    CassetteTrail t(1);
    start(t);
    for (int i = 1; i <= 20; ++i)
    {
        t.sync(TAPE, t.cursor().offset);
        t.played(size_t(i) * 100, 10000, 600);
        REQUIRE(t.size() <= 2);
    }
    const CassetteBoundary got = t.rewind_by(30);
    CHECK(got.time_ms <= 170000);
}

TEST_CASE("the trail allocates once, when a tape first needs it")
{
    alloc_calls = 0;
    alloc_fails = false;
    CassetteTrail t(8, test_alloc);
    CHECK(alloc_calls == 0);

    start(t);
    CHECK(alloc_calls == 1);
    CHECK(alloc_bytes == 8 * sizeof(CassetteBoundary));

    t.sync(TAPE + 1, 0); // another tape
    t.restart();
    CHECK(alloc_calls == 1);
}

TEST_CASE("when allocation fails the tape plays on and a rewind answers the start")
{
    alloc_calls = 0;
    alloc_fails = true;
    CassetteTrail t(8, test_alloc);
    start(t);

    CHECK_FALSE(t.valid());
    CHECK(t.size() == 0);
    t.played(100, 10000, 600); // nothing kept, nothing broken
    t.played(200, 10000, 600);
    CHECK(t.size() == 0);

    expect_boundary(t.rewind_by(5), 0, 0, 600);
    CHECK_FALSE(t.valid());

    SUBCASE("and it recovers when the tape is at the start and memory is back")
    {
        alloc_fails = false;
        t.sync(TAPE, 0);
        CHECK(t.valid());
        CHECK(t.size() == 1);
        play(t, ABCD);
        expect_boundary(t.rewind_by(15), 200, 20000, 600);
    }
}
