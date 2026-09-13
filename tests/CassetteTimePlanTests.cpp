#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "sio/cassette_time_plan.h"
#include "sio/fsk_plan.h"

#include <cstdio>
#include <cstdint>

// CassetteTimePlanTests.cpp — doctest coverage for the pure Custom Rewind
// real-duration model + chunk-boundary walker in
// lib/device/sio/cassette_time_plan.{h,cpp}.
//
// FIXTURE POLICY (explicit, per project decision): synthetic/fabricated .cas
// fixtures are PROHIBITED. Every test that needs actual chunk content reads
// the ONE authorized real CAS file, turbo_software_zorro.cas, from its local
// path (NOT committed to this repo — see below). A structural-error case
// with no naturally-occurring real-CAS example (truncated header, baud==0,
// filesize==0) is tested ONLY at the pure scalar-function level, exactly as
// the project's own fsk_compute_bounds() host tests already do in
// FskPlanTests.cpp — no CAS file, real or fabricated, is constructed for
// those cases.
//
// turbo_software_zorro.cas — authorized for LOCAL test runs only:
//   SHA256 982fbe1e7df44b426841ee879c8d0767ecec5900e730671f03473252db9a4562
//   size   504316 bytes
//   path (this development machine): /tmp/zorro_sd.cas
// This file is NOT copied into the repository (explicit instruction). Tests
// that depend on it locate it via CASSETTE_TIME_TESTS_ZORRO_CAS_PATH (set by
// the environment, defaulting to /tmp/zorro_sd.cas) and SKIP gracefully
// (doctest MESSAGE + return, not a failure) when the file is absent — e.g.
// on a CI machine that does not have this local reference file — verifying
// its size as a lightweight integrity signal (the SHA256 above was verified
// by hand against this exact file in the session that authorized it).

namespace
{
    struct FileReaderCtx
    {
        std::FILE *f;
    };

    size_t file_reader(void *ctx, size_t offset, uint8_t *dst, size_t n)
    {
        FileReaderCtx *c = static_cast<FileReaderCtx *>(ctx);
        if (std::fseek(c->f, static_cast<long>(offset), SEEK_SET) != 0)
            return 0;
        return std::fread(dst, 1, n, c->f);
    }

    const char *zorro_path()
    {
        const char *p = std::getenv("CASSETTE_TIME_TESTS_ZORRO_CAS_PATH");
        return (p != nullptr) ? p : "/tmp/zorro_sd.cas";
    }

    // Returns nullptr (and leaves *out_size untouched) if the authorized real
    // fixture is not present on this machine, or does not match the expected
    // size. Callers MUST skip (not fail) when this returns nullptr.
    std::FILE *open_zorro_cas(size_t &out_size)
    {
        std::FILE *f = std::fopen(zorro_path(), "rb");
        if (f == nullptr)
            return nullptr;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz != 504316)
        {
            std::fclose(f);
            return nullptr;
        }
        out_size = static_cast<size_t>(sz);
        return f;
    }

    // doctest has no built-in way to distinguish a data-dependent RUNTIME skip
    // (this fixture not present on this machine) from a genuine pass in its
    // summary counts — "X passed | 0 failed | 0 skipped" prints identically
    // either way. These counters + the custom main() below make that
    // distinction explicit and impossible to miss, instead of letting a skip
    // silently read as a pass.
    int g_real_cas_ran = 0;
    int g_real_cas_skipped = 0;

    // Every REAL CAS test's guard clause funnels through here so the two
    // counters above are always accurate. Returns the opened FILE* (caller
    // must fclose it) or nullptr if the fixture is unavailable — on nullptr
    // the caller must return immediately without checking anything.
    std::FILE *require_real_cas(size_t &filesize)
    {
        std::FILE *f = open_zorro_cas(filesize);
        if (f == nullptr)
        {
            ++g_real_cas_skipped;
            MESSAGE("SKIPPED (turbo_software_zorro.cas not found at ", zorro_path(),
                    ") — this test case did not exercise any real data");
            return nullptr;
        }
        ++g_real_cas_ran;
        return f;
    }
} // namespace

// -----------------------------------------------------------------------------
// Scalar / pure-function tests — no CAS file, real or fabricated.
// -----------------------------------------------------------------------------

TEST_CASE("cas_bits_duration_us: baud==0 returns 0, never divides by zero")
{
    CHECK(cas_bits_duration_us(128, 0) == 0);
}

TEST_CASE("cas_bits_duration_us: exact integer case at 600 baud")
{
    // 128 bytes * 10 bits/byte = 1280 bits; 1280 * 1'000'000 / 600 = 2'133'333 (floor)
    CHECK(cas_bits_duration_us(128, 600) == 2133333ULL);
}

TEST_CASE("cas_bits_duration_us: large byte_count does not overflow in intermediate product")
{
    // 65535 bytes (max one A8CAS chunk_length) * 10 bits/byte * 1'000'000 ~=
    // 6.55e11, which overflows uint32_t (max ~4.29e9) even before the divide
    // — must be computed in uint64_t. At baud=300 the post-divide RESULT
    // (2.18e9) happens to still fit uint32, so use baud=1 to also make the
    // final result exceed uint32 range and prove the whole chain is uint64.
    uint64_t d = cas_bits_duration_us(65535, 1);
    CHECK(d == (uint64_t)65535 * 10ULL * 1000000ULL / 1ULL);
    CHECK(d > 0xFFFFFFFFULL); // proves the result itself exceeded 32 bits
}

TEST_CASE("cas_data_record_duration_us: IRG (exact) + bits (floor) combine correctly")
{
    uint64_t expect_bits = cas_bits_duration_us(132, 600);
    CHECK(cas_data_record_duration_us(238, 132, 600) == 238ULL * 1000ULL + expect_bits);
}

TEST_CASE("cas_legacy_block_duration_us: fixed 132-byte-on-wire + 300ms delay at 600 baud")
{
    // 132 bytes * 10 bits/byte = 1320 bits; 1320 * 1e6 / 600 = 2'200'000 us
    // exactly (integer), + 300'000 us fixed delay = 2'500'000 us.
    CHECK(cas_legacy_block_duration_us() == 2500000ULL);
}

TEST_CASE("cas_walk_tape_time: filesize==0 fails structurally, no fabricated result")
{
    CassetteWalkState out{};
    auto reader = [](void *, size_t, uint8_t *, size_t) -> size_t { return 0; };
    CHECK_FALSE(cas_walk_tape_time(0, reader, nullptr, SIZE_MAX, UINT64_MAX, out));
}

TEST_CASE("cas_walk_tape_time: reader==nullptr fails structurally")
{
    CassetteWalkState out{};
    CHECK_FALSE(cas_walk_tape_time(100, nullptr, nullptr, SIZE_MAX, UINT64_MAX, out));
}

TEST_CASE("cas_t2k_samples_to_us: samplerate==0 returns 0, never divides by zero")
{
    CHECK(cas_t2k_samples_to_us(32, 0) == 0);
}

// -----------------------------------------------------------------------------
// Real-CAS tests — turbo_software_zorro.cas (authorized, local-only).
// Structural facts below (offsets, nominal durations) were derived by
// directly parsing this exact file's real chunk headers in the session that
// authorized it as the reference corpus member; they are not invented.
// -----------------------------------------------------------------------------

TEST_CASE("REAL CAS: turbo_software_zorro.cas total nominal duration matches the parsed corpus")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    CassetteWalkState out{};
    bool ok = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, UINT64_MAX, out);
    std::fclose(f);

    REQUIRE(ok);
    // With both stop conditions disabled the walk is unbounded, so it
    // correctly reaches the TRUE end of tape: offset == filesize and
    // time_us == the grand total nominal duration (835182900 us at the start
    // of the final chunk + that chunk's own ~93145900 us = 928328800 us),
    // exactly matching the value independently computed when this file was
    // first parsed to authorize it as the reference corpus member.
    CHECK(out.offset == filesize);
    CHECK(out.time_us == 928328800ULL);
}

TEST_CASE("REAL CAS: clamp to zero when requested rewind exceeds total duration")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // Total nominal duration is ~928.3s; request far more than that (target
    // time 0).
    CassetteWalkState out{};
    bool ok = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 0ULL, out);
    std::fclose(f);

    REQUIRE(ok);
    // Offset 8, not 0: the leading 8-byte "FUJI" container header is itself
    // a ZERO-duration structural chunk, so its start time (0) TIES with the
    // first real content chunk's start time (also 0). The walker's "advance
    // while <= target" rule correctly resolves ties by preferring the LATEST
    // boundary at that time — i.e. it skips the meaningless zero-length
    // header and lands on the first real, resumable chunk. This is the same
    // rule that (correctly) prevents it from stopping short inside a
    // nonzero-duration chunk elsewhere in the walk.
    CHECK(out.offset == 8);
    CHECK(out.time_us == 0);
}

TEST_CASE("REAL CAS: target inside a run chunk resolves to THAT chunk's own offset, not the run start")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // Chunk at offset 262168 spans real time [481925800, 596368600) — the
    // 5th member of the single 8-chunk FSK run starting at offset 8. Target
    // 500,000,000 us falls inside it.
    CassetteWalkState out{};
    bool ok = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 500000000ULL, out);
    std::fclose(f);

    REQUIRE(ok);
    CHECK(out.offset == 262168); // this chunk's own offset — never the run's
                                  // first chunk (offset 8), never the next chunk
    CHECK(out.time_us == 481925800ULL);
}

TEST_CASE("REAL CAS: exact chunk boundary time resolves inclusively to that chunk")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // 141163100 us is the exact real start time of the chunk at offset 65548
    // (second member of the run). The result must be THIS chunk, not the
    // previous one (offset 8).
    CassetteWalkState out{};
    bool ok = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 141163100ULL, out);
    std::fclose(f);

    REQUIRE(ok);
    CHECK(out.offset == 65548);
    CHECK(out.time_us == 141163100ULL);
}

TEST_CASE("REAL CAS: 1 microsecond before/after an exact FSK boundary resolves to different chunks")
{
    // Valid ONLY because this boundary's time is pure FSK arithmetic (irg_ms*1000
    // + sum(v*100), no division anywhere) — exact to the microsecond by
    // construction. A data/baud-chunk boundary would carry floor-division
    // truncation error and could not support this same ±1us assertion (see
    // the TIME ROUNDING audit — not re-tested here, no real CAS with a data
    // chunk is part of this authorized corpus).
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    CassetteWalkState before{};
    bool ok1 = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 141163099ULL, before);
    CassetteWalkState at{};
    bool ok2 = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 141163100ULL, at);
    CassetteWalkState after{};
    bool ok3 = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 141163101ULL, after);
    std::fclose(f);

    REQUIRE(ok1);
    REQUIRE(ok2);
    REQUIRE(ok3);
    CHECK(before.offset == 8);      // still the PREVIOUS chunk (1us before the boundary)
    CHECK(at.offset == 65548);      // exactly on the boundary -> this chunk
    CHECK(after.offset == 65548);   // 1us after -> still this chunk
}

TEST_CASE("REAL CAS: OFFSET->TIME walking to a known real offset reproduces its exact start time")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    CassetteWalkState out{};
    // offset 393248 is the 7th run chunk's real header offset.
    bool ok = cas_walk_tape_time(filesize, file_reader, &ctx, 393248, UINT64_MAX, out);
    std::fclose(f);

    REQUIRE(ok);
    CHECK(out.offset == 393248);
    CHECK(out.time_us == 709783400ULL);
}

// -----------------------------------------------------------------------------
// Custom main(): runs doctest normally, then prints an explicit, unmissable
// REAL CAS line that a plain "X passed | 0 failed | 0 skipped" summary cannot
// distinguish (doctest has no data-dependent runtime-skip bucket — a skipped
// REAL CAS test and a genuinely exercised one both count as "passed"). Exit
// code still reflects doctest's own pass/fail result; this only adds
// visibility, it does not change what counts as success.
// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    int res = ctx.run();

    std::printf("\n=== REAL CAS FIXTURE STATUS (turbo_software_zorro.cas) ===\n");
    if (g_real_cas_ran > 0 && g_real_cas_skipped == 0)
    {
        std::printf("PASS: all %d REAL CAS test case(s) found the fixture at '%s' "
                    "and genuinely exercised it.\n",
                    g_real_cas_ran, zorro_path());
    }
    else if (g_real_cas_ran > 0 && g_real_cas_skipped > 0)
    {
        std::printf("PARTIAL: %d REAL CAS test case(s) ran against the real fixture, "
                    "%d SKIPPED (fixture not found/re-checked mid-run at '%s').\n",
                    g_real_cas_ran, g_real_cas_skipped, zorro_path());
    }
    else
    {
        std::printf("SKIP: turbo_software_zorro.cas was NOT found at '%s' — all %d "
                    "REAL CAS test case(s) were SKIPPED, not verified. Only the "
                    "scalar/pure-function tests above ran for real. Set "
                    "CASSETTE_TIME_TESTS_ZORRO_CAS_PATH to point at the file to "
                    "actually exercise them.\n",
                    zorro_path(), g_real_cas_skipped);
    }
    std::printf("===========================================================\n");

    return res;
}
