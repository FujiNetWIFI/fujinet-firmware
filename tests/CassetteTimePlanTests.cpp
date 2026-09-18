#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "sio/cassette_time_plan.h"
#include "sio/fsk_plan.h"

#include <cstdio>
#include <cstdint>
#include <vector>

// CassetteTimePlanTests.cpp — doctest coverage for the pure Custom Rewind
// real-duration model + chunk-boundary walker in
// lib/device/sio/cassette_time_plan.{h,cpp}.
//
// FIXTURE POLICY: synthetic/fabricated .cas fixtures are prohibited. Tests
// needing real chunk content read the one authorized real CAS file,
// turbo_software_zorro.cas (SHA256
// 982fbe1e7df44b426841ee879c8d0767ecec5900e730671f03473252db9a4562, 504316
// bytes; NOT committed to this repo). A structural-error case with no
// naturally-occurring real-CAS example is tested only at the pure
// scalar-function level, as FskPlanTests.cpp already does.
//
// Tests locate the fixture via CASSETTE_TIME_TESTS_ZORRO_CAS_PATH (default
// /tmp/zorro_sd.cas) and SKIP gracefully (not fail) when absent, e.g. on CI.

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

    // doctest can't distinguish a data-dependent runtime skip from a genuine
    // pass in its summary counts; these + the custom main() below make that
    // distinction explicit instead of letting a skip read as a pass.
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

    // Active FSK Rewind fixture: scans the same contiguous zero-IRG FSK run
    // sioCassette::play_fsk_chunk() would preload, using the same pure
    // primitives, so value_counts[] matches production's _fsk_run_value_counts.
    // Each chunk's payload is read fully into memory ONCE (mirroring the
    // resident PSRAM block table), so the resolver reads only already-loaded
    // bytes, no per-value file I/O — matching fsk_resident_run_value_reader.
    struct FskRunFixture
    {
        static constexpr size_t kMaxChunks = FSK_RUN_MAX_CHUNKS;
        static constexpr size_t kMaxChunkBytes = 65536;
        size_t payload_offsets[kMaxChunks] = {}; // payload start (header+8) per chunk — for
                                                  // resident-reader offset-translation checks
        size_t value_counts[kMaxChunks] = {};
        size_t count = 0;
        std::vector<uint8_t> payload[kMaxChunks];
    };

    bool scan_fsk_run(std::FILE *f, size_t filesize, size_t chunk0_header_offset,
                      FskRunFixture &out)
    {
        out.count = 0;
        FileReaderCtx ctx{f};

        uint8_t hdr[8];
        if (file_reader(&ctx, chunk0_header_offset, hdr, 8) != 8)
            return false;
        uint16_t clen = fsk_decode_le16(&hdr[4]);
        FskBounds bounds = fsk_compute_bounds(filesize, chunk0_header_offset, clen);
        if (!bounds.header_complete)
            return false;

        out.payload_offsets[0] = chunk0_header_offset + 8;
        out.value_counts[0] = fsk_value_count(bounds.data_avail);
        out.payload[0].resize(bounds.data_avail);
        if (bounds.data_avail > 0 &&
            file_reader(&ctx, out.payload_offsets[0], out.payload[0].data(), bounds.data_avail) != bounds.data_avail)
            return false;
        out.count = 1;

        size_t scan = bounds.next_offset;
        while (scan != 0 && out.count < FskRunFixture::kMaxChunks)
        {
            uint8_t chdr[8];
            if (file_reader(&ctx, scan, chdr, 8) != 8)
                break;
            const bool is_fsk = (chdr[0] == 'f' && chdr[1] == 's' &&
                                 chdr[2] == 'k' && chdr[3] == ' ');
            const uint16_t cclen = fsk_decode_le16(&chdr[4]);
            const uint16_t cirg = fsk_decode_le16(&chdr[6]);
            const FskBounds cb = fsk_compute_bounds(filesize, scan, cclen);
            if (!fsk_run_should_join(is_fsk, cirg, cb.header_complete,
                                     cb.structurally_truncated))
                break;
            const size_t idx = out.count;
            out.payload_offsets[idx] = scan + 8;
            out.value_counts[idx] = fsk_value_count(cb.data_avail);
            out.payload[idx].resize(cb.data_avail);
            if (cb.data_avail > 0 &&
                file_reader(&ctx, out.payload_offsets[idx], out.payload[idx].data(), cb.data_avail) != cb.data_avail)
                break;
            out.count++;
            scan = cb.next_offset;
        }
        return true;
    }

    uint16_t real_run_value_reader(void *ctx, size_t chunk_index, size_t value_index)
    {
        FskRunFixture *fx = static_cast<FskRunFixture *>(ctx);
        return fsk_decode_le16(&fx->payload[chunk_index][value_index * 2]);
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
// cas_fsk_resolve_active_rewind: structural-only cases (no naturally-occurring
// real-CAS example needed — same policy as the fsk_compute_bounds structural
// tests in FskPlanTests.cpp).
// -----------------------------------------------------------------------------

TEST_CASE("cas_fsk_resolve_active_rewind: run_chunk_count==0 always fails (nothing resident)")
{
    auto never_called = [](void *, size_t, size_t) -> uint16_t
    { FAIL("reader must not be called"); return 0; };
    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        nullptr, 0, never_called, nullptr, 0, 0, 12345);
    CHECK_FALSE(r.resolved);
}

TEST_CASE("cas_fsk_resolve_active_rewind: target before run_start_time_us fails (fine resolution -> caller must fall back)")
{
    size_t value_counts[1] = {10};
    auto never_called = [](void *, size_t, size_t) -> uint16_t
    { FAIL("reader must not be called"); return 0; };
    // run starts at 1,000,000us; target is before that entirely.
    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        value_counts, 1, never_called, nullptr, 1000000ULL, 999000ULL, 500000ULL);
    CHECK_FALSE(r.resolved);
}

TEST_CASE("cas_fsk_resolve_active_rewind: target inside the leading IRG resolves to chunk 0, no resume, reader never consulted")
{
    size_t value_counts[1] = {10};
    auto never_called = [](void *, size_t, size_t) -> uint16_t
    { FAIL("reader must not be called for an inside-leading-IRG result"); return 0; };
    // run_start=0, leading_irg=999000 (Zorro's real leading IRG); target lands
    // mid-IRG at 500000.
    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        value_counts, 1, never_called, nullptr, 0ULL, 999000ULL, 500000ULL);
    REQUIRE(r.resolved);
    CHECK(r.inside_leading_irg);
}

TEST_CASE("cas_fsk_resolve_active_rewind: target exactly at run_start_time_us (leading_irg==0) resolves to chunk 0 value 0, not inside-IRG")
{
    size_t value_counts[1] = {3};
    uint16_t values[3] = {100, 200, 300}; // ticks: 10000, 20000, 30000 us
    auto reader = [](void *ctx, size_t, size_t v) -> uint16_t
    { return static_cast<uint16_t *>(ctx)[v]; };
    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        value_counts, 1, reader, values, 0ULL, 0ULL, 0ULL);
    REQUIRE(r.resolved);
    CHECK_FALSE(r.inside_leading_irg);
    CHECK(r.chunk_index == 0);
    CHECK(r.value_index == 0);
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
    // Offset 8, not 0: the leading "FUJI" header is a zero-duration chunk
    // tied with the first real chunk's start time (both 0); the walker's
    // "advance while <= target" rule resolves the tie toward the later,
    // resumable boundary rather than the meaningless zero-length header.
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
// Active FSK Rewind — cas_fsk_resolve_active_rewind() driven by the real
// resident run of turbo_software_zorro.cas's single 8-chunk FSK run (starting
// at file offset 8). run_start_time_us==0 and the chunk offsets 8/65548/
// 262168/393248 with their start times are the same real, independently
// parsed facts the cas_walk_tape_time tests above already establish — reused
// here, not re-invented.
// -----------------------------------------------------------------------------

TEST_CASE("REAL CAS Active FSK Rewind: -5s resolves within the SAME run chunk")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    bool scanned = scan_fsk_run(f, filesize, 8, fx);
    std::fclose(f);
    REQUIRE(scanned);
    REQUIRE(fx.count == 8);

    // Chunk run-index 4 (real file offset 262168) spans real time
    // [481925800, 596368600). 500,000,000 - 5,000,000 = 495,000,000 stays
    // inside that same interval.
    const uint64_t leading_irg_us = 999000; // Zorro's real leading IRG (999ms)
    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        fx.value_counts, fx.count, real_run_value_reader, &fx,
        0ULL, leading_irg_us, 495000000ULL);

    REQUIRE(r.resolved);
    CHECK_FALSE(r.inside_leading_irg);
    CHECK(r.chunk_index == 4);
    CHECK(fx.payload_offsets[r.chunk_index] - 8 == 262168); // resident-reader offset translation
}

TEST_CASE("REAL CAS Active FSK Rewind: -60s crosses a real run-chunk boundary")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    bool scanned = scan_fsk_run(f, filesize, 8, fx);
    std::fclose(f);
    REQUIRE(scanned);

    // Chunk run-index 1 (real file offset 65548) starts at 141,163,100.
    // current = start + 30s; target = current - 60s = start - 30s, landing
    // back inside run-index 0 (which spans [999000, 141163100) — the leading
    // IRG is 999000, so this target is safely inside chunk 0's payload span,
    // not the IRG itself: 141163100 - 30000000 = 111163100 > 999000).
    const uint64_t leading_irg_us = 999000;
    const uint64_t current = 141163100ULL + 30000000ULL;
    const uint64_t target = current - 60000000ULL;
    REQUIRE(target > leading_irg_us); // sanity: still inside chunk 0's payload, not its IRG

    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        fx.value_counts, fx.count, real_run_value_reader, &fx,
        0ULL, leading_irg_us, target);

    REQUIRE(r.resolved);
    CHECK_FALSE(r.inside_leading_irg);
    CHECK(r.chunk_index == 0); // crossed back out of run-index 1 into run-index 0
    CHECK(fx.payload_offsets[r.chunk_index] - 8 == 8);
}

TEST_CASE("REAL CAS Active FSK Rewind: target exactly on a value boundary resolves inclusively, never mid-value")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    bool scanned = scan_fsk_run(f, filesize, 8, fx);
    std::fclose(f);
    REQUIRE(scanned);

    const uint64_t leading_irg_us = 999000;

    // First resolve an arbitrary target to learn one real value's exact start
    // time, by independently re-summing (an oracle loop, deliberately NOT the
    // production algorithm) up to and including the resolved value.
    FskActiveRewindResolution r1 = cas_fsk_resolve_active_rewind(
        fx.value_counts, fx.count, real_run_value_reader, &fx,
        0ULL, leading_irg_us, 495000000ULL);
    REQUIRE(r1.resolved);
    REQUIRE_FALSE(r1.inside_leading_irg);

    uint64_t exact_start = leading_irg_us;
    for (size_t c = 0; c <= r1.chunk_index; ++c)
    {
        const size_t vmax = (c == r1.chunk_index) ? r1.value_index : fx.value_counts[c];
        for (size_t v = 0; v < vmax; ++v)
            exact_start += fsk_ticks_for_value(real_run_value_reader(&fx, c, v));
    }

    // Resolving AT exactly that value's own start time must return the SAME
    // (chunk_index, value_index) — inclusive boundary, matching
    // cas_walk_tape_time's own documented "exact boundary resolves
    // inclusively" behavior.
    FskActiveRewindResolution r2 = cas_fsk_resolve_active_rewind(
        fx.value_counts, fx.count, real_run_value_reader, &fx,
        0ULL, leading_irg_us, exact_start);
    REQUIRE(r2.resolved);
    CHECK_FALSE(r2.inside_leading_irg);
    CHECK(r2.chunk_index == r1.chunk_index);
    CHECK(r2.value_index == r1.value_index);

    // And 1us before it must resolve to the PREVIOUS value (or, if this was
    // value 0 of its chunk, the previous chunk's last value) — never the same
    // one, proving the resolver never overshoots the target.
    if (exact_start > 0)
    {
        FskActiveRewindResolution r0 = cas_fsk_resolve_active_rewind(
            fx.value_counts, fx.count, real_run_value_reader, &fx,
            0ULL, leading_irg_us, exact_start - 1);
        REQUIRE(r0.resolved);
        const bool same_value = (r0.chunk_index == r2.chunk_index && r0.value_index == r2.value_index);
        CHECK_FALSE(same_value);
    }
}

TEST_CASE("REAL CAS Active FSK Rewind: fine resolution fails when target predates the run -> caller falls back to cas_walk_tape_time")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    bool scanned = scan_fsk_run(f, filesize, 8, fx);
    REQUIRE(scanned);

    // This run IS the whole tape, so there's no earlier real position to
    // target the fallback trigger with — assert the trigger condition
    // directly (run_start_time_us > target_us) instead, then confirm the
    // existing coarse walker still resolves target_us on its own.
    FskActiveRewindResolution r = cas_fsk_resolve_active_rewind(
        fx.value_counts, fx.count, real_run_value_reader, &fx,
        /*run_start_time_us=*/1000000ULL, 999000ULL, /*target_us=*/500000ULL);
    CHECK_FALSE(r.resolved);

    FileReaderCtx ctx{f};
    CassetteWalkState dest{};
    bool ok = cas_walk_tape_time(filesize, file_reader, &ctx, SIZE_MAX, 500000ULL, dest);
    std::fclose(f);
    REQUIRE(ok);
    CHECK(dest.offset == 8);
    CHECK(dest.time_us == 0);
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
