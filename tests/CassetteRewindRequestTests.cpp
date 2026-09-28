#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "device/sio/cassetteRewindRequest.h"

// ---------------------------------------------------------------------------------------------
// The rewind request: what the web UI leaves for the task that plays the tape.
// ---------------------------------------------------------------------------------------------

namespace
{
using Kind = CassetteRewind::Kind;

// the request as the tape's owner sees it, for a tape being played
CassetteRewind taken(CassetteRewindRequest &r)
{
    return r.take(true);
}
} // namespace

TEST_CASE("a rewind request is handed over once")
{
    CassetteRewindRequest r;
    CHECK(taken(r).kind == Kind::none);

    r.add_seconds(30);
    const CassetteRewind first = taken(r);
    CHECK(first.kind == Kind::seconds);
    CHECK(first.seconds == 30);

    CHECK(taken(r).kind == Kind::none);
}

TEST_CASE("quick seconds requests add up into one")
{
    CassetteRewindRequest r;
    r.add_seconds(5);
    r.add_seconds(10);
    r.add_seconds(30);

    const CassetteRewind got = taken(r);
    CHECK(got.kind == Kind::seconds);
    CHECK(got.seconds == 45);
    CHECK(taken(r).kind == Kind::none);
}

TEST_CASE("seconds saturate instead of wrapping into the start marker or into a short rewind")
{
    CassetteRewindRequest r;
    r.add_seconds(UINT32_MAX);
    r.add_seconds(UINT32_MAX);
    r.add_seconds(1);

    const CassetteRewind got = taken(r);
    CHECK(got.kind == Kind::seconds);
    CHECK(got.seconds == UINT32_MAX - 1);
}

TEST_CASE("a rewind to the start is handed over once")
{
    CassetteRewindRequest r;
    r.to_start();
    CHECK(taken(r).kind == Kind::to_start);
    CHECK(taken(r).kind == Kind::none);
}

TEST_CASE("the start of the tape beats seconds, whichever was asked first")
{
    SUBCASE("seconds, then the start")
    {
        CassetteRewindRequest r;
        r.add_seconds(30);
        r.to_start();
        CHECK(taken(r).kind == Kind::to_start);
        CHECK(taken(r).kind == Kind::none);
    }
    SUBCASE("the start, then seconds")
    {
        CassetteRewindRequest r;
        r.to_start();
        r.add_seconds(30);
        r.add_seconds(UINT32_MAX);
        CHECK(taken(r).kind == Kind::to_start);
        CHECK(taken(r).kind == Kind::none);
    }
    SUBCASE("a request made after the start was taken is a new request")
    {
        CassetteRewindRequest r;
        r.to_start();
        CHECK(taken(r).kind == Kind::to_start);
        r.add_seconds(7);
        const CassetteRewind got = taken(r);
        CHECK(got.kind == Kind::seconds);
        CHECK(got.seconds == 7);
    }
}

TEST_CASE("a mount or unmount drops the request, so another tape never inherits it")
{
    CassetteRewindRequest r;

    r.add_seconds(30);
    r.discard(); // tape A unmounted
    CHECK(taken(r).kind == Kind::none);

    r.to_start();
    r.discard(); // tape B mounted over it
    CHECK(taken(r).kind == Kind::none);

    // and a request made for the tape now mounted still works
    r.add_seconds(12);
    const CassetteRewind got = taken(r);
    CHECK(got.kind == Kind::seconds);
    CHECK(got.seconds == 12);
}

TEST_CASE("a tape being recorded ignores seconds but takes the start")
{
    CassetteRewindRequest r;

    r.add_seconds(30);
    CHECK(r.take(false).kind == Kind::none);
    // it was dropped, not kept for whatever plays next
    CHECK(taken(r).kind == Kind::none);

    r.add_seconds(30);
    r.to_start();
    CHECK(r.take(false).kind == Kind::to_start);
    CHECK(r.take(false).kind == Kind::none);
}

TEST_CASE("requests from several tasks are all counted while the owner takes them")
{
    constexpr int producers = 4;
    constexpr int per_producer = 20000;

    CassetteRewindRequest r;
    std::atomic<bool> go{false};
    std::atomic<int> running{producers};

    std::vector<std::thread> threads;
    for (int i = 0; i < producers; ++i)
        threads.emplace_back(
            [&]
            {
                while (!go)
                    std::this_thread::yield();
                for (int n = 0; n < per_producer; ++n)
                    r.add_seconds(1);
                --running;
            });

    uint64_t total = 0;
    go = true;
    while (running > 0)
    {
        const CassetteRewind got = taken(r);
        if (got.kind == Kind::seconds)
            total += got.seconds;
    }
    for (std::thread &t : threads)
        t.join();

    const CassetteRewind rest = taken(r);
    if (rest.kind == Kind::seconds)
        total += rest.seconds;

    CHECK(total == uint64_t(producers) * per_producer);
}
