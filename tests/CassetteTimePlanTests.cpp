#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "sio/cassette_time_plan.h"
#include "sio/fsk_plan.h"
#include "FskRmtPingPongModel.h"
#include "FskTransportModel.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

// CassetteTimePlanTests.cpp — doctest coverage for the pure Custom Rewind
// real-duration model + chunk-boundary walker in
// lib/device/sio/cassette_time_plan.{h,cpp}.
//
// FIXTURE POLICY: synthetic/fabricated .cas FILES are prohibited. Tests
// needing real chunk content read the one authorized real CAS file,
// turbo_software_zorro.cas (SHA256
// 982fbe1e7df44b426841ee879c8d0767ecec5900e730671f03473252db9a4562, 504316
// bytes; NOT committed to this repo). A structural rule with no
// naturally-occurring real-CAS example (a non-zero IRG on a chunk other than
// the first, several runs, a `data` chunk next to an `fsk ` chunk) is tested
// at the pure level against a few in-memory chunk headers built inside the
// test itself ("structural image" below). Those are never files, never used as
// evidence about real tapes, and only exercise the pure position arithmetic.
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

    // Resident-run fixture: scans the same contiguous zero-IRG FSK run
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
// Run-relative position (R, Q): cas_run_pos_split and the IRG pause/resume
// arithmetic. Q counts microseconds from the walker time at R, i.e. from BEFORE
// R's leading IRG, so one scalar covers the IRG and the waveform.
// -----------------------------------------------------------------------------

TEST_CASE("cas_run_pos_split: Q=0 is the very start of the leading IRG")
{
    const CasRunPosSplit s = cas_run_pos_split(1000000, 0);
    CHECK(s.in_irg);
    CHECK(s.irg_remaining_us == 1000000);
    CHECK(s.wave_ticks == 0);
}

TEST_CASE("cas_run_pos_split: middle of the IRG leaves the exact remainder")
{
    const CasRunPosSplit s = cas_run_pos_split(1000000, 800000);
    CHECK(s.in_irg);
    CHECK(s.irg_remaining_us == 200000);
    CHECK(s.wave_ticks == 0);
}

TEST_CASE("cas_run_pos_split: the final microsecond of the IRG still has 1 us to go")
{
    const CasRunPosSplit s = cas_run_pos_split(1000000, 999999);
    CHECK(s.in_irg);
    CHECK(s.irg_remaining_us == 1);
    CHECK(s.wave_ticks == 0);
}

TEST_CASE("cas_run_pos_split: Q == irg is exactly the waveform start")
{
    const CasRunPosSplit s = cas_run_pos_split(1000000, 1000000);
    CHECK_FALSE(s.in_irg);
    CHECK(s.irg_remaining_us == 0);
    CHECK(s.wave_ticks == 0);
}

TEST_CASE("cas_run_pos_split: inside the waveform, wave_ticks = Q - irg")
{
    const CasRunPosSplit s = cas_run_pos_split(1000000, 1500000);
    CHECK_FALSE(s.in_irg);
    CHECK(s.irg_remaining_us == 0);
    CHECK(s.wave_ticks == 500000);
}

TEST_CASE("cas_run_pos_split: a chunk without IRG is all waveform")
{
    CHECK_FALSE(cas_run_pos_split(0, 0).in_irg);
    CHECK(cas_run_pos_split(0, 0).wave_ticks == 0);
    CHECK(cas_run_pos_split(0, 1234).wave_ticks == 1234);
    // largest A8CAS IRG (65535 ms) with a >32-bit Q
    const uint64_t big_irg = 65535ULL * 1000ULL;
    const CasRunPosSplit s = cas_run_pos_split(big_irg, big_irg + 5000000000ULL);
    CHECK_FALSE(s.in_irg);
    CHECK(s.wave_ticks == 5000000000ULL);
}

TEST_CASE("IRG pause: 1000 ms IRG frozen at 800 ms leaves 200 ms, however long MOTOR stays OFF")
{
    // The frozen state is the scalar Q only; no wall-clock value exists in the
    // model, so nothing that happens while paused can change what remains.
    const uint64_t irg_us = 1000000;
    const uint64_t q = 800000;
    for (int waited_s : { 0, 1, 60, 3600 })
    {
        (void)waited_s;
        const CasRunPosSplit s = cas_run_pos_split(irg_us, q);
        CHECK(s.in_irg);
        CHECK(s.irg_remaining_us == 200000);
    }
}

TEST_CASE("IRG pause/resume: repeated pauses accumulate exactly and cross into the waveform")
{
    const uint64_t irg_us = 1000000;
    uint64_t q = 0;

    q += 800000; // first pause, 800 ms in
    CHECK(cas_run_pos_split(irg_us, q).irg_remaining_us == 200000);

    q += 100000; // resumed, held 100 ms, paused again
    CHECK(cas_run_pos_split(irg_us, q).in_irg);
    CHECK(cas_run_pos_split(irg_us, q).irg_remaining_us == 100000);

    q += 100000; // resumed and the IRG ran out
    CHECK_FALSE(cas_run_pos_split(irg_us, q).in_irg);
    CHECK(cas_run_pos_split(irg_us, q).wave_ticks == 0);

    q += 42; // 42 ticks into the waveform, then paused
    CHECK(cas_run_pos_split(irg_us, q).wave_ticks == 42);
}

// -----------------------------------------------------------------------------
// cas_fsk_locate_ticks over in-memory value arrays (pure arithmetic; not CAS
// files).
// -----------------------------------------------------------------------------

namespace
{
    struct MemRun
    {
        std::vector<std::vector<uint16_t>> chunks;

        std::vector<size_t> counts() const
        {
            std::vector<size_t> c;
            for (const auto &ch : chunks)
                c.push_back(ch.size());
            return c;
        }
    };

    uint16_t mem_run_reader(void *ctx, size_t chunk, size_t value)
    {
        return static_cast<MemRun *>(ctx)->chunks[chunk][value];
    }

    FskLocateResult locate(MemRun &run, uint64_t ticks)
    {
        const std::vector<size_t> c = run.counts();
        return cas_fsk_locate_ticks(c.data(), c.size(), mem_run_reader, &run, ticks);
    }

    // Oracle: total ticks before (chunk, value) plus the skip.
    uint64_t oracle_ticks(MemRun &run, const FskLocateResult &r)
    {
        uint64_t t = 0;
        for (size_t c = 0; c <= r.chunk_index && c < run.chunks.size(); ++c)
        {
            const size_t vmax = (c == r.chunk_index) ? r.value_index : run.chunks[c].size();
            for (size_t v = 0; v < vmax; ++v)
                t += fsk_ticks_for_value(run.chunks[c][v]);
        }
        return t + r.skip_ticks;
    }
}

TEST_CASE("cas_fsk_locate_ticks: exact value boundaries land on that value with no skip")
{
    MemRun run{ { { 10, 20, 30 } } }; // 1000, 2000, 3000 us
    FskLocateResult r = locate(run, 0);
    CHECK((r.chunk_index == 0 && r.value_index == 0 && r.skip_ticks == 0 && !r.at_end));
    r = locate(run, 1000);
    CHECK((r.value_index == 1 && r.skip_ticks == 0));
    r = locate(run, 3000);
    CHECK((r.value_index == 2 && r.skip_ticks == 0));
}

TEST_CASE("cas_fsk_locate_ticks: a position inside a long value keeps the offset into it")
{
    // Head of a real tape image: LOW 0, HIGH 7, LOW 1, HIGH 65535 (6553500 us).
    MemRun run{ { { 0, 7, 1, 65535, 0, 65535 } } };
    const FskLocateResult r = locate(run, 1000000); // 800 ticks before value 3
    CHECK(r.chunk_index == 0);
    CHECK(r.value_index == 3);
    CHECK(r.skip_ticks == 999200);
    CHECK_FALSE(r.at_end);
    CHECK(oracle_ticks(run, r) == 1000000);
}

TEST_CASE("cas_fsk_locate_ticks: zero-duration values consume their index but never hold the position")
{
    MemRun run{ { { 5, 0, 0, 7 } } }; // 500, 0, 0, 700
    FskLocateResult r = locate(run, 500);
    CHECK(r.value_index == 3); // both zeros stepped over
    CHECK(r.skip_ticks == 0);
    r = locate(run, 499);
    CHECK(r.value_index == 0);
    CHECK(r.skip_ticks == 499);
}

TEST_CASE("cas_fsk_locate_ticks: parity comes from the chunk-local index, so it resets per chunk")
{
    MemRun run{ { { 3, 4 }, { 5, 6 } } };
    // End of chunk 0 (700 us) is the start of chunk 1's value 0.
    const FskLocateResult r = locate(run, 700);
    CHECK(r.chunk_index == 1);
    CHECK(r.value_index == 0);
    CHECK_FALSE(fsk_level_for_index(r.value_index)); // even -> LOW again, not the odd parity continuing
    CHECK(fsk_level_for_index(locate(run, 300).value_index)); // chunk 0 value 1 -> HIGH
}

TEST_CASE("cas_fsk_locate_ticks: joined zero-IRG run positions are continuous across chunks")
{
    MemRun run{ { { 10, 20 }, { 30, 40 }, { 50 } } }; // 3000 + 7000 + 5000 us
    for (uint64_t t : { 0ULL, 999ULL, 1000ULL, 2999ULL, 3000ULL, 6999ULL, 7000ULL, 9999ULL, 10000ULL, 14999ULL })
    {
        const FskLocateResult r = locate(run, t);
        REQUIRE_FALSE(r.at_end);
        CHECK(oracle_ticks(run, r) == t);
    }
}

TEST_CASE("cas_fsk_locate_ticks: at or beyond the run's total duration reports at_end")
{
    MemRun run{ { { 3, 4 } } }; // 700 us
    CHECK(locate(run, 700).at_end);
    CHECK(locate(run, 100000).at_end);
    CHECK_FALSE(locate(run, 699).at_end);

    MemRun empty{};
    CHECK(locate(empty, 0).at_end);
}

// -----------------------------------------------------------------------------
// Structural image: a few in-memory chunk headers (NOT a CAS file) covering
// what the single real fixture cannot: a previous chunk with its own IRG,
// several runs, and a `data` chunk next to `fsk ` chunks.
//
//   FUJI @0
//   A  fsk  irg 2000 ms, 4 x 1000  @8   start 0        (IRG 2.0 s + wave 0.4 s)
//   B  fsk  irg 1000 ms, [0,500]   @24  start 2400000  (IRG 1.0 s + wave 0.05 s)
//   C  fsk  irg 0,       [0,300]   @36  start 3450000  (joined to B)
//   D  data irg 100 ms, 4 bytes    @48  start 3480000  (600 baud)
// -----------------------------------------------------------------------------

namespace
{
    void put_u16(std::vector<uint8_t> &v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }

    void put_chunk(std::vector<uint8_t> &v, const char *type, uint16_t irg,
                   const std::vector<uint8_t> &payload)
    {
        for (int i = 0; i < 4; ++i)
            v.push_back(static_cast<uint8_t>(type[i]));
        put_u16(v, static_cast<uint16_t>(payload.size()));
        put_u16(v, irg);
        v.insert(v.end(), payload.begin(), payload.end());
    }

    std::vector<uint8_t> values_payload(std::initializer_list<uint16_t> vals)
    {
        std::vector<uint8_t> p;
        for (uint16_t x : vals)
            put_u16(p, x);
        return p;
    }

    std::vector<uint8_t> structural_image()
    {
        std::vector<uint8_t> v;
        put_chunk(v, "FUJI", 0, {});
        put_chunk(v, "fsk ", 2000, values_payload({ 1000, 1000, 1000, 1000 }));
        put_chunk(v, "fsk ", 1000, values_payload({ 0, 500 }));
        put_chunk(v, "fsk ", 0, values_payload({ 0, 300 }));
        put_chunk(v, "data", 100, { 1, 2, 3, 4 });
        return v;
    }

    size_t mem_time_reader(void *ctx, size_t offset, uint8_t *dst, size_t n)
    {
        const std::vector<uint8_t> *img = static_cast<const std::vector<uint8_t> *>(ctx);
        if (offset >= img->size())
            return 0;
        const size_t k = std::min(n, img->size() - offset);
        std::memcpy(dst, img->data() + offset, k);
        return k;
    }

    CassetteTargetResolution resolve_img(std::vector<uint8_t> &img, uint64_t target, bool &ok)
    {
        CassetteTargetResolution r{};
        ok = cas_resolve_target_time(img.size(), mem_time_reader, &img, target, r);
        return r;
    }
}

TEST_CASE("structural image: chunk boundaries are where the walker says (sanity for the cases below)")
{
    std::vector<uint8_t> img = structural_image();
    struct { size_t off; uint64_t t; } expect[] = { { 8, 0 }, { 24, 2400000 }, { 36, 3450000 }, { 48, 3480000 } };
    for (const auto &e : expect)
    {
        CassetteWalkState w{};
        REQUIRE(cas_walk_tape_time(img.size(), mem_time_reader, &img, e.off, UINT64_MAX, w));
        CHECK(w.offset == e.off);
        CHECK(w.time_us == e.t);
    }
}

TEST_CASE("rewind target inside a previous chunk's IRG resolves to that chunk with Q' < irg")
{
    std::vector<uint8_t> img = structural_image();
    bool ok = false;
    // Paused at Q=300000 inside B's IRG: origin = 2400000 + 300000. -1 s lands in A's IRG.
    const uint64_t origin = 2400000 + 300000;
    const CassetteTargetResolution r = resolve_img(img, origin - 1000000, ok);
    REQUIRE(ok);
    CHECK(r.is_fsk);
    CHECK(r.walk.offset == 8);
    CHECK(r.irg_ms == 2000);
    CHECK(r.q_us == 1700000);
    const CasRunPosSplit s = cas_run_pos_split(static_cast<uint64_t>(r.irg_ms) * 1000ULL, r.q_us);
    CHECK(s.in_irg);
    CHECK(s.irg_remaining_us == 300000);
}

TEST_CASE("rewind crossing a run boundary: from run 2 back into run 1's waveform")
{
    std::vector<uint8_t> img = structural_image();
    bool ok = false;
    // Origin inside B's IRG (run 2); target inside A's waveform (run 1).
    const CassetteTargetResolution r = resolve_img(img, 2200000, ok);
    REQUIRE(ok);
    CHECK(r.walk.offset == 8);
    CHECK(r.q_us == 2200000);
    const CasRunPosSplit s = cas_run_pos_split(2000000, r.q_us);
    CHECK_FALSE(s.in_irg);
    CHECK(s.wave_ticks == 200000);
}

TEST_CASE("rewind target inside a joined zero-IRG chunk keeps Q' in the joined chunk")
{
    std::vector<uint8_t> img = structural_image();
    bool ok = false;
    const CassetteTargetResolution r = resolve_img(img, 3460000, ok); // 10 ms into C
    REQUIRE(ok);
    CHECK(r.is_fsk);
    CHECK(r.walk.offset == 36);
    CHECK(r.irg_ms == 0);
    CHECK(r.q_us == 10000);
    CHECK(cas_run_pos_split(0, r.q_us).wave_ticks == 10000);
}

TEST_CASE("rewind target saturates at the tape start: first chunk, Q' = 0")
{
    std::vector<uint8_t> img = structural_image();
    bool ok = false;
    const CassetteTargetResolution r = resolve_img(img, 0, ok);
    REQUIRE(ok);
    CHECK(r.is_fsk);
    CHECK(r.walk.offset == 8);
    CHECK(r.q_us == 0);
}

TEST_CASE("rewind target inside a data chunk is a coarse (non-FSK) reposition")
{
    std::vector<uint8_t> img = structural_image();
    bool ok = false;
    const CassetteTargetResolution r = resolve_img(img, 3500000, ok);
    REQUIRE(ok);
    CHECK_FALSE(r.is_fsk);
    CHECK(r.walk.offset == 48);
    CHECK(r.walk.time_us == 3480000);
}

TEST_CASE("cas_resolve_target_time: a non-FUJI buffer is never read as fsk chunks")
{
    std::vector<uint8_t> raw(300, 0x55);
    raw[128] = 'f'; raw[129] = 's'; raw[130] = 'k'; raw[131] = ' ';
    CassetteTargetResolution r{};
    REQUIRE(cas_resolve_target_time(raw.size(), mem_time_reader, &raw, 400000, r));
    CHECK_FALSE(r.is_fsk);
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
// MOTOR-pause position model on the real Zorro CAS. The tape is ONE joined run
// starting at offset 8 (R = 8, walker time 0); chunk 1 carries a 999 ms IRG.
// Frozen position Q is microseconds from the walker time at R.
// -----------------------------------------------------------------------------

namespace
{
    struct ZorroExpect
    {
        uint32_t seconds;
        uint64_t target_us;
        size_t   offset;     // R'
        uint64_t q_us;       // Q'
        size_t   value;      // chunk-local value in the resident run starting at R'
        uint64_t skip;
    };

    // Derived from the real file (same arithmetic as the walker, computed in an
    // independent oracle when the fixture was authorized): frozen at 633.610 s.
    constexpr uint64_t kZorroFrozenQ = 633610000ULL;
    const ZorroExpect kZorroExpect[] = {
        {  5, 628610000ULL, 327708, 32241400ULL, 8868, 3900 },
        { 10, 623610000ULL, 327708, 27241400ULL, 7372, 11200 },
        { 30, 603610000ULL, 327708,  7241400ULL, 2132, 2600 },
        { 60, 573610000ULL, 262168, 91684200ULL, 26298, 2200 },
    };
}

TEST_CASE("REAL CAS position model: frozen at 633.610 s, -5/-10/-30/-60 resolve as derived")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    CassetteWalkState at_r{};
    REQUIRE(cas_walk_tape_time(filesize, file_reader, &ctx, 8, UINT64_MAX, at_r));
    CHECK(at_r.offset == 8);
    CHECK(at_r.time_us == 0);
    const uint64_t origin = at_r.time_us + kZorroFrozenQ;
    CHECK(origin == 633610000ULL);

    for (const ZorroExpect &e : kZorroExpect)
    {
        const uint64_t target = origin - static_cast<uint64_t>(e.seconds) * 1000000ULL;
        CHECK(target == e.target_us);

        CassetteTargetResolution r{};
        REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, target, r));
        CHECK(r.is_fsk);
        CHECK(r.irg_ms == 0);
        CHECK(r.walk.offset == e.offset);
        CHECK(r.q_us == e.q_us);

        // Locate the resulting waveform position in the resident run that
        // would be preloaded from R' (chunk 0 of that run is R').
        FskRunFixture fx;
        REQUIRE(scan_fsk_run(f, filesize, r.walk.offset, fx));
        const CasRunPosSplit split = cas_run_pos_split(0, r.q_us);
        const FskLocateResult loc = cas_fsk_locate_ticks(
            fx.value_counts, fx.count, real_run_value_reader, &fx, split.wave_ticks);
        REQUIRE_FALSE(loc.at_end);
        CHECK(loc.chunk_index == 0);
        CHECK(loc.value_index == e.value);
        CHECK(loc.skip_ticks == e.skip);

        // Oracle: ticks before the located value plus the skip is exactly P'.
        uint64_t t = 0;
        for (size_t c = 0; c <= loc.chunk_index; ++c)
        {
            const size_t vmax = (c == loc.chunk_index) ? loc.value_index : fx.value_counts[c];
            for (size_t v = 0; v < vmax; ++v)
                t += fsk_ticks_for_value(real_run_value_reader(&fx, c, v));
        }
        CHECK(t + loc.skip_ticks == split.wave_ticks);
    }
    std::fclose(f);
}

TEST_CASE("REAL CAS position model: waiting while paused changes nothing")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // The frozen state is (R, Q). Nothing time-dependent is stored, so a 60 s
    // (or any) human delay before the -10s request cannot move the origin: the
    // same inputs must give the same answer every time.
    CassetteWalkState at_r{};
    REQUIRE(cas_walk_tape_time(filesize, file_reader, &ctx, 8, UINT64_MAX, at_r));

    CassetteTargetResolution first{};
    for (int waited_s : { 0, 60, 3600 })
    {
        (void)waited_s;
        const uint64_t origin = at_r.time_us + kZorroFrozenQ;
        CassetteTargetResolution r{};
        REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, origin - 10000000ULL, r));
        if (waited_s == 0)
            first = r;
        CHECK(r.walk.offset == first.walk.offset);
        CHECK(r.q_us == first.q_us);
    }
    CHECK(first.walk.offset == 327708);
    CHECK(first.q_us == 27241400ULL);
    std::fclose(f);
}

TEST_CASE("REAL CAS position model: repeated rewinds while paused accumulate")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // -10s, then -10s again from the position the first rewind committed
    // (R' = 327708, Q' = 27241400): origin' = start(R') + Q' = 623610000.
    CassetteWalkState at_r2{};
    REQUIRE(cas_walk_tape_time(filesize, file_reader, &ctx, 327708, UINT64_MAX, at_r2));
    CHECK(at_r2.time_us == 596368600ULL);
    const uint64_t origin2 = at_r2.time_us + 27241400ULL;
    CHECK(origin2 == 623610000ULL);

    CassetteTargetResolution r{};
    REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, origin2 - 10000000ULL, r));
    CHECK(r.walk.offset == 327708);
    CHECK(r.q_us == 17241400ULL); // total -20 s from the original 633.610 s
    std::fclose(f);
}

TEST_CASE("REAL CAS position model: inside Zorro's 999 ms leading IRG")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // Frozen 800 ms into the IRG: 199 ms remain, however long MOTOR stays OFF.
    const CasRunPosSplit paused = cas_run_pos_split(999000, 800000);
    CHECK(paused.in_irg);
    CHECK(paused.irg_remaining_us == 199000);

    // -5s from inside the IRG saturates at the tape start (R = 8, Q' = 0).
    CassetteTargetResolution r{};
    REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, 0, r));
    CHECK(r.walk.offset == 8);
    CHECK(r.irg_ms == 999);
    CHECK(r.q_us == 0);

    // A target 200 ms into the IRG stays inside it.
    REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, 200000, r));
    CHECK(r.walk.offset == 8);
    CHECK(r.q_us == 200000);
    CHECK(cas_run_pos_split(static_cast<uint64_t>(r.irg_ms) * 1000ULL, r.q_us).irg_remaining_us == 799000);
    std::fclose(f);
}

TEST_CASE("REAL CAS position model: a long HIGH value (6.5535 s) is resumed inside, not replayed")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, 8, fx));
    std::fclose(f);

    // Values 0..3 are 0, 7, 1, 65535 -> value 3 starts 800 ticks in and lasts
    // 6553500 ticks. 2 s into the waveform lies inside it.
    const FskLocateResult r = cas_fsk_locate_ticks(
        fx.value_counts, fx.count, real_run_value_reader, &fx, 2000000);
    CHECK(r.chunk_index == 0);
    CHECK(r.value_index == 3);
    CHECK(r.skip_ticks == 1999200);
    CHECK(fsk_ticks_for_value(real_run_value_reader(&fx, 0, 3)) == 6553500);
}

TEST_CASE("REAL CAS position model: resolving Q as a target returns the same (R, Q) at every boundary")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // Chunk starts: 8 -> 0, 65548 -> 141163100, 393248 -> 709783400.
    const uint64_t qs[] = { 0, 1, 998999, 999000, 999001, 141163099ULL, 141163100ULL,
                            500000000ULL, 709783400ULL, 928328799ULL };
    for (uint64_t q : qs)
    {
        CassetteTargetResolution r{};
        REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, q, r));
        REQUIRE(r.is_fsk);
        CHECK(r.walk.time_us + r.q_us == q); // (R', Q') maps back to the same absolute time
    }
    std::fclose(f);
}

TEST_CASE("Setup failure: the re-armed position resumes identically (round trip through the split)")
{
    // What play_fsk_chunk does with the position it re-armed: the retry sees the
    // same in_irg / remaining / wave split as the failed attempt did.
    const uint64_t irg_us = 2000000;
    for (uint64_t q : { (uint64_t)0, (uint64_t)700000, irg_us, irg_us + 1, irg_us + 1500000 })
    {
        const CasRunPosSplit before = cas_run_pos_split(irg_us, q);
        const FskSetupRetry r = fsk_setup_failure_retry(true, q, irg_us, before.wave_ticks, false);
        REQUIRE(r.rearm);
        const CasRunPosSplit after = cas_run_pos_split(irg_us, r.q_us);
        CHECK(after.in_irg == before.in_irg);
        CHECK(after.irg_remaining_us == before.irg_remaining_us);
        CHECK(after.wave_ticks == before.wave_ticks);
    }
}

namespace
{
    // The values of a resident run, in playback order, from (first_chunk,
    // first_value) to the end of the joined run.
    std::vector<uint16_t> real_run_values(const FskRunFixture &fx, size_t first_chunk,
                                          size_t first_value)
    {
        std::vector<uint16_t> out;
        for (size_t c = first_chunk; c < fx.count; ++c)
            for (size_t v = (c == first_chunk ? first_value : 0); v < fx.value_counts[c]; ++v)
                out.push_back(fsk_decode_le16(&fx.payload[c][v * 2]));
        return out;
    }
}

TEST_CASE("REAL CAS RMT bookkeeping: Zorro's joined run confirms exactly the consumed halves at every threshold")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;

    // The whole joined zero-IRG run starting at R = 8 (8 chunks, 928 s of tape),
    // exactly what play_fsk_chunk preloads and hands to one rmt_transmit.
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, 8, fx));
    std::fclose(f);
    REQUIRE(fx.count == 8);
    const std::vector<uint16_t> values = real_run_values(fx, 0, 0);
    REQUIRE(values.size() > 100000);

    const int64_t t0 = 5000000, lat = 40;
    fskmodel::RmtPingPongModel m(values, 0, 0, t0);
    const fskmodel::Simulation sim = fskmodel::simulate(m, values, 0, 0, t0, lat, 1000000);
    REQUIRE(sim.completed);
    REQUIRE(sim.thresholds.size() > 400);         // a substantial part: the whole run

    // Total encoded duration is exactly the nominal run duration.
    uint64_t nominal = 0;
    for (size_t c = 0; c < fx.count; ++c)
        for (size_t v = 0; v < fx.value_counts[c]; ++v)
            nominal += fsk_ticks_for_value(fsk_decode_le16(&fx.payload[c][v * 2]));
    CHECK(sim.total_ticks == nominal);

    size_t mismatches = 0;
    for (const fskmodel::ThresholdRecord &r : sim.thresholds)
    {
        if (r.confirmed != r.expected_ref)
            ++mismatches;
        if (!r.done_after)
        {
            CHECK(r.next_pending == r.cumulative_before);
            CHECK(r.next_pending != r.cumulative_after);
        }
    }
    CHECK(mismatches == 0);

    // The first three thresholds, expected (oracle: the raw boundary minus the
    // last three entries, i.e. what the pin had reached) versus actual (recorded
    // reference).
    REQUIRE(sim.thresholds.size() >= 3);
    std::printf("REAL CAS RMT bookkeeping (Zorro R=8, whole joined run, %zu thresholds):\n",
                sim.thresholds.size());
    for (size_t i = 0; i < 3; ++i)
    {
        const fskmodel::ThresholdRecord &r = sim.thresholds[i];
        std::printf("  threshold %zu: raw boundary %llu  expected reference %llu  actual %llu  (end-of-refill total %llu)\n",
                    r.n, (unsigned long long)r.expected, (unsigned long long)r.expected_ref,
                    (unsigned long long)r.confirmed, (unsigned long long)r.cumulative_after);
    }
    CHECK(sim.thresholds[0].confirmed == sim.thresholds[0].expected_ref);
    CHECK(sim.thresholds[1].confirmed == sim.thresholds[1].expected_ref);
    CHECK(sim.thresholds[2].confirmed == sim.thresholds[2].expected_ref);
    // ... and behind the raw boundary: never the position the transmitter merely fetched.
    for (size_t i = 0; i < 3; ++i)
        CHECK(sim.thresholds[i].confirmed < sim.thresholds[i].expected);
    // The defect would have recorded the end-of-refill total of the PREVIOUS
    // callback here: one half ahead from the second threshold on.
    CHECK(sim.thresholds[1].confirmed != sim.thresholds[0].cumulative_after);
    CHECK(sim.thresholds[2].confirmed != sim.thresholds[1].cumulative_after);
}

TEST_CASE("REAL CAS RMT bookkeeping: a stop between Zorro thresholds never receives the refilled half")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, 8, fx));
    std::fclose(f);
    const std::vector<uint16_t> values = real_run_values(fx, 0, 0);

    const int64_t t0 = 5000000, lat = 40;
    fskmodel::RmtPingPongModel m(values, 0, 0, t0);
    // Stop the simulation after 12 thresholds: the log then holds the most
    // recent references, as a task frozen mid-run would see them.
    const fskmodel::Simulation sim = fskmodel::simulate(m, values, 0, 0, t0, lat, 12);
    REQUIRE(sim.thresholds.size() == 12);
    const fskmodel::ThresholdRecord &a = sim.thresholds[10]; // threshold 11
    const fskmodel::ThresholdRecord &b = sim.thresholds[11]; // threshold 12
    REQUIRE(b.expected > a.expected + 1000);

    // Stops between the two threshold events (in hardware time).
    const int64_t cross_a = t0 + static_cast<int64_t>(a.hw_position);
    const int64_t cross_b = t0 + static_cast<int64_t>(b.hw_position);
    for (int64_t stop : { cross_a + lat, cross_a + lat + 1, (cross_a + cross_b) / 2, cross_b - 1 })
    {
        const uint64_t truth = static_cast<uint64_t>(stop - t0);
        const uint64_t pos = fsk_physical_ticks_at(m.log, sim.total_ticks, stop);
        CHECK(pos <= truth);                       // never ahead of the tape
        CHECK(truth - pos <= static_cast<uint64_t>(lat)); // and behind by no more than the callback latency
        CHECK(pos >= a.expected_ref);              // at least what the pin had reached at threshold a
        CHECK(pos < b.expected_ref);               // never the half that was still resident
        CHECK(pos < a.cumulative_after);           // nor anything the refills encoded
    }
}

TEST_CASE("REAL CAS RMT bookkeeping: resumed inside a value (-10 s position) counts the seed exactly once")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;

    // Zorro frozen at 633.610 s, -10 s: R' = 327708, Q' = 27'241'400, which is
    // value 7372 with 11'200 ticks already played.
    const ZorroExpect &e = kZorroExpect[1];
    REQUIRE(e.seconds == 10);
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, e.offset, fx));
    std::fclose(f);
    const uint64_t seed = e.q_us;                  // waveform ticks already played (irg == 0)

    const std::vector<uint16_t> rest = real_run_values(fx, 0, e.value);
    const std::vector<uint16_t> whole = real_run_values(fx, 0, 0);

    // Independent total: the whole run from R' equals seed + the remainder.
    uint64_t whole_ticks = 0;
    for (uint16_t v : whole)
        whole_ticks += fsk_ticks_for_value(v);
    uint64_t rest_ticks = 0;
    for (uint16_t v : rest)
        rest_ticks += fsk_ticks_for_value(v);
    REQUIRE(rest_ticks - e.skip + seed == whole_ticks);

    const int64_t t0 = 9000000, lat = 33;
    fskmodel::RmtPingPongModel m(rest, seed, e.skip, t0);
    const fskmodel::Simulation sim = fskmodel::simulate(m, rest, seed, e.skip, t0, lat, 1000000);
    REQUIRE(sim.completed);
    REQUIRE(sim.thresholds.size() > 20);
    CHECK(sim.total_ticks == whole_ticks);         // seed counted once: ends at the run total

    size_t mismatches = 0;
    for (const fskmodel::ThresholdRecord &r : sim.thresholds)
    {
        if (r.confirmed != r.expected_ref)
            ++mismatches;
        CHECK(r.confirmed > seed);
        CHECK(r.confirmed <= whole_ticks);
    }
    CHECK(mismatches == 0);
    std::printf("REAL CAS RMT bookkeeping (Zorro R'=327708 resumed at %llu ticks, skip %llu): "
                "first threshold expected %llu actual %llu\n",
                (unsigned long long)seed, (unsigned long long)e.skip,
                (unsigned long long)sim.thresholds[0].expected_ref,
                (unsigned long long)sim.thresholds[0].confirmed);
}

// ════════════════════════════════════════════════════════════════════════════
// Fast MOTOR resume. The FSK transmission is aborted in hardware at freeze time
// and the preloaded run stays resident, so a MOTOR OFF -> ON cycle costs
// milliseconds instead of the drain of the queued symbols plus an SD preload.
// FskTransportModel.h models the timeline with costs measured on the FujiNet.
// ════════════════════════════════════════════════════════════════════════════

namespace
{
    // FSK-shaped run: 1.2-1.7 ms bit cells with an 11.6 ms plateau every tenth value,
    // like the record structure of the real Zorro tape (~5 ms per RMT symbol).
    std::vector<uint16_t> fsk_like_run(size_t n)
    {
        static const uint16_t pat[4] = {12, 13, 16, 17};
        std::vector<uint16_t> v;
        v.reserve(n);
        for (size_t i = 0; i < n; ++i)
            v.push_back(i % 10 == 9 ? 116 : pat[i % 4]);
        return v;
    }

    constexpr size_t kRunValues = 60000;               // ~150 s of tape, 120 KB of payload
    constexpr size_t kRunBytes = kRunValues * 2;

    fsktransport::Cfg legacy_cfg()
    {
        fsktransport::Cfg c;
        c.fast_abort = false;      // v4: play out everything queued in the RMT memory
        c.keep_resident = false;   // v4: free the run, preload it again on resume
        return c;
    }
}

TEST_CASE("Fast resume: MOTOR OFF freezes the position the tape reached and MOTOR ON resumes from it")
{
    fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, fsktransport::Cfg{});
    const fsktransport::Cycle c1 = tp.run(0, 20000000);   // MOTOR ON at 0, OFF 20 s later
    REQUIRE(c1.started);
    CHECK(c1.froze);
    CHECK(c1.preloaded);                                   // the very first start loads the run
    CHECK(c1.frozen_ticks > 10000000);
    CHECK(c1.frozen_ticks <= c1.true_ticks);               // never ahead of the tape
    CHECK(c1.true_ticks - c1.frozen_ticks <= 2000);        // within ~2 ms of it

    const fsktransport::Cycle c2 = tp.run(c1.motor_off_us + 400000, c1.motor_off_us + 5000000);
    REQUIRE(c2.started);
    CHECK(c2.seed_ticks == c1.frozen_ticks);               // resumes exactly where it froze
    CHECK_FALSE(c2.preloaded);                             // the resident run is reused
    CHECK(c2.resume_latency_us <= 5000);                   // ms, not seconds

    // The waveform of the new transmission starts at the frozen position: the
    // production locate agrees with an independent tick count.
    MemRun run;
    run.chunks.push_back(fsk_like_run(kRunValues));
    const FskLocateResult loc = locate(run, c2.seed_ticks);
    REQUIRE_FALSE(loc.at_end);
    CHECK(oracle_ticks(run, loc) == c2.seed_ticks);
}

TEST_CASE("Fast resume: nothing of the old transmission can reach DATA IN after the resume")
{
    fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, fsktransport::Cfg{});
    fsktransport::Cycle prev = tp.run(0, 15000000);
    REQUIRE(prev.froze);
    int64_t t = prev.motor_off_us;
    for (int i = 0; i < 12; ++i)
    {
        const int64_t on = t + 3000 + 7000 * (i % 3);      // MOTOR back ON within 3-17 ms
        const int64_t off = on + 60000 + 30000 * (i % 4);
        const fsktransport::Cycle c = tp.run(on, off);
        REQUIRE(c.started);
        // The previous transaction ended, and its channel was destroyed, BEFORE
        // this transmission started: no overlap, no old symbol after the resume.
        CHECK(prev.old_hw_done_us <= c.tx_start_us);
        CHECK(prev.channel_free_us <= c.tx_start_us);
        CHECK(prev.old_pin_end_us < c.tx_start_us);
        // The pin was muted right after the freeze; the hardware finished within
        // a couple of symbols (<= 13.3 ms each), not the queued half-seconds.
        CHECK(prev.old_pin_end_us <= prev.motor_off_us + 1000);
        CHECK(prev.old_hw_done_us - prev.motor_off_us <= 40000);
        CHECK(c.seed_ticks == prev.frozen_ticks);
        prev = c;
        t = c.motor_off_us;
    }
}

TEST_CASE("Fast resume: many quick MOTOR OFF/ON cycles keep a small, constant latency")
{
    fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, fsktransport::Cfg{});
    fsktransport::Cycle c = tp.run(0, 12000000);
    REQUIRE(c.froze);
    const uint64_t start_pos = c.frozen_ticks;
    int64_t t = c.motor_off_us;
    int64_t worst_early = 0, worst_late = 0;
    uint64_t prev_pos = c.frozen_ticks;
    int64_t played_us = 0;
    for (int i = 0; i < 60; ++i)
    {
        const int64_t on = t + 3000 + (i * 7) % 38000;      // gaps of 3-41 ms
        const int64_t off = on + 80000 + (i * 13) % 100000; // 80-180 ms of tape each time
        c = tp.run(on, off);
        REQUIRE(c.started);
        CHECK_FALSE(c.preloaded);
        CHECK(c.froze);
        CHECK(c.frozen_ticks >= prev_pos);                   // the position never goes back
        prev_pos = c.frozen_ticks;
        played_us += c.motor_off_us - c.tx_start_us;
        if (i < 10)
            worst_early = std::max(worst_early, c.resume_latency_us);
        if (i >= 50)
            worst_late = std::max(worst_late, c.resume_latency_us);
        CHECK(c.resume_latency_us <= 60000);                 // bounded by a couple of symbols + fixed costs
        t = c.motor_off_us;
    }
    CHECK(tp.preloads() == 1);                               // only the very first start read the SD
    CHECK(worst_late <= worst_early + 2000);                 // no degradation over the cycles
    // Progress equals what was played, minus a couple of ms of interpolation per freeze.
    const uint64_t progressed = prev_pos - start_pos;
    CHECK(progressed <= static_cast<uint64_t>(played_us));
    CHECK(progressed + 60 * 2000 >= static_cast<uint64_t>(played_us));
}

TEST_CASE("Fast resume: the old behavior costs seconds per cycle, the fix costs milliseconds")
{
    auto measure = [](fsktransport::Cfg cfg, int cycles)
    {
        fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, cfg);
        fsktransport::Cycle c = tp.run(0, 20000000);
        int64_t worst = 0, sum = 0;
        int64_t t = c.motor_off_us;
        for (int i = 0; i < cycles; ++i)
        {
            c = tp.run(t + 4000, t + 4000 + 8000000);   // MOTOR stays on long enough to restart
            REQUIRE(c.started);
            worst = std::max(worst, c.resume_latency_us);
            sum += c.resume_latency_us;
            t = c.motor_off_us;
        }
        return std::make_pair(worst, sum);
    };
    const auto legacy = measure(legacy_cfg(), 8);
    const auto fixed = measure(fsktransport::Cfg{}, 8);
    CHECK(legacy.first >= 1500000);       // drain of the queued symbols + SD preload: seconds
    CHECK(legacy.second >= 8 * 1500000);
    CHECK(fixed.first <= 20000);          // ms
    CHECK(fixed.second <= 8 * 20000);
}

TEST_CASE("Fast resume: an HTTP rewind drops the cached run and the next start preloads once")
{
    fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, fsktransport::Cfg{});
    fsktransport::Cycle c = tp.run(0, 20000000);
    REQUIRE(c.froze);
    c = tp.run(c.motor_off_us + 5000, c.motor_off_us + 400000);
    REQUIRE(c.started);
    CHECK_FALSE(c.preloaded);
    CHECK(tp.resident());

    // Rewind 5 s back while the tape is frozen (before MOTOR comes back).
    const uint64_t target = c.frozen_ticks - 5000000;
    tp.http_rewind(target);
    CHECK_FALSE(tp.resident());                               // never reused after a reposition
    const fsktransport::Cycle after = tp.run(c.motor_off_us + 8000, c.motor_off_us + 30000000);
    REQUIRE(after.started);
    CHECK(after.seed_ticks == target);
    CHECK(after.preloaded);                                   // the rewound run is loaded again, once

    // From then on it is resident again and resumes fast.
    const fsktransport::Cycle again = tp.run(after.motor_off_us + 5000, after.motor_off_us + 300000);
    REQUIRE(again.started);
    CHECK_FALSE(again.preloaded);
    CHECK(again.resume_latency_us <= 20000);
    CHECK(tp.preloads() == 2);
}

TEST_CASE("Fast resume: MOTOR OFF before the waveform starts leaves the position and the run untouched")
{
    fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, fsktransport::Cfg{});
    fsktransport::Cycle c = tp.run(0, 20000000);
    REQUIRE(c.froze);
    const uint64_t pos = tp.position();
    // MOTOR ON and OFF again within a millisecond, before the new waveform begins.
    const fsktransport::Cycle glitch = tp.run(c.motor_off_us + 5000, c.motor_off_us + 5600);
    CHECK_FALSE(glitch.started);
    CHECK(glitch.froze);
    CHECK(tp.position() == pos);
    CHECK(tp.resident());
    const fsktransport::Cycle next = tp.run(glitch.motor_off_us + 5000, glitch.motor_off_us + 300000);
    REQUIRE(next.started);
    CHECK(next.seed_ticks == pos);
    CHECK_FALSE(next.preloaded);
}

TEST_CASE("Stop claim: MOTOR and HTTP still exclude each other, and neither wins after the channel is gone")
{
    FskStopReason slot = FskStopReason::NONE;
    CHECK(fsk_stop_try_claim(slot, true, FskStopReason::MOTOR));
    CHECK_FALSE(fsk_stop_try_claim(slot, true, FskStopReason::HTTP));   // exactly one owner
    CHECK(slot == FskStopReason::MOTOR);

    slot = FskStopReason::NONE;
    CHECK(fsk_stop_try_claim(slot, true, FskStopReason::HTTP));
    CHECK_FALSE(fsk_stop_try_claim(slot, true, FskStopReason::MOTOR));
    CHECK(slot == FskStopReason::HTTP);

    slot = FskStopReason::NONE;                                          // channel unpublished / destroyed
    CHECK_FALSE(fsk_stop_try_claim(slot, false, FskStopReason::MOTOR));
    CHECK_FALSE(fsk_stop_try_claim(slot, false, FskStopReason::HTTP));
    CHECK(slot == FskStopReason::NONE);
}

TEST_CASE("REAL CAS fast resume: after -5 s the loader's MOTOR toggles keep a normal pace")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FileReaderCtx ctx{f};

    // The real failure: MOTOR OFF at Q = 337.537 s of tape (R = 8). Rewind 5 s.
    const uint64_t frozen_q = 337536976ULL;
    CassetteTargetResolution r{};
    REQUIRE(cas_resolve_target_time(filesize, file_reader, &ctx, frozen_q - 5000000ULL, r));
    REQUIRE(r.is_fsk);
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, r.walk.offset, fx));
    std::fclose(f);

    size_t run_bytes = 0;
    for (size_t c = 0; c < fx.count; ++c)
        run_bytes += fx.payload[c].size();
    const std::vector<uint16_t> values = real_run_values(fx, 0, 0);
    REQUIRE(values.size() > 50000);
    CAPTURE(r.walk.offset);
    CAPTURE(r.q_us);
    CAPTURE(run_bytes);

    auto session = [&](fsktransport::Cfg cfg, int64_t play_us)
    {
        struct Out { int preloads; int64_t first_latency; int64_t worst_toggle; int64_t sum_toggle; uint64_t progressed; int64_t played; };
        Out o{};
        fsktransport::Transport tp(values, run_bytes, cfg);
        tp.set_position(r.q_us);                                // the -5 s position (irg == 0)
        // First MOTOR ON after the rewind: the rewound run is loaded once.
        fsktransport::Cycle c = tp.run(0, 30000000);
        REQUIRE(c.started);
        o.first_latency = c.resume_latency_us;
        o.played += c.motor_off_us - c.tx_start_us;
        int64_t t = c.motor_off_us;
        uint64_t prev = c.frozen_ticks;
        // The loader now toggles MOTOR over and over.
        for (int i = 0; i < 30; ++i)
        {
            const int64_t on = t + 4000 + (i % 5) * 3000;
            c = tp.run(on, on + play_us + (i * 17) % 200000);
            REQUIRE(c.started);
            o.worst_toggle = std::max(o.worst_toggle, c.resume_latency_us);
            o.sum_toggle += c.resume_latency_us;
            o.played += c.motor_off_us - c.tx_start_us;
            CHECK(c.frozen_ticks >= prev);
            prev = c.frozen_ticks;
            t = c.motor_off_us;
        }
        o.preloads = tp.preloads();
        o.progressed = prev - r.q_us;
        return o;
    };

    const auto fixed = session(fsktransport::Cfg{}, 250000);
    CHECK(fixed.preloads == 1);                                 // only the first start after the rewind
    CHECK(fixed.worst_toggle <= 30000);                         // ms per toggle
    CHECK(fixed.sum_toggle <= 30 * 30000);
    // Progress is what was played, less a couple of ms of interpolation per freeze.
    CHECK(fixed.progressed <= static_cast<uint64_t>(fixed.played));
    CHECK(fixed.progressed + 31 * 2000 >= static_cast<uint64_t>(fixed.played));

    const auto legacy = session(legacy_cfg(), 8000000);         // MOTOR must stay on to outlast the pauses
    CHECK(legacy.preloads == 31);                               // every cycle re-read the SD
    CHECK(legacy.worst_toggle >= 1500000);                      // seconds per toggle, as reported
    CHECK(legacy.sum_toggle >= 30 * 1500000LL);
    std::printf("REAL CAS fast resume after -5 s (R'=%zu, Q'=%llu us, run %zu B): first start %.2f s "
                "(one-time preload); 30 toggles: fixed worst %.1f ms / sum %.1f ms; old v4 model worst %.2f s / sum %.1f s\n",
                r.walk.offset, (unsigned long long)r.q_us, run_bytes, fixed.first_latency / 1e6,
                fixed.worst_toggle / 1e3, fixed.sum_toggle / 1e3, legacy.worst_toggle / 1e6,
                legacy.sum_toggle / 1e6);
}

TEST_CASE("Fast resume: consecutive MOTOR OFF/ON cycles that span refills never move the tape ahead")
{
    // 3-6 s of tape per cycle: every cycle crosses at least one refill threshold
    // (a half is ~1.3 s here), so the freeze is resolved from a refill reference.
    fsktransport::Transport tp(fsk_like_run(kRunValues), kRunBytes, fsktransport::Cfg{});
    fsktransport::Cycle c = tp.run(0, 20000000);
    REQUIRE(c.froze);
    const uint64_t start_pos = c.frozen_ticks;
    int64_t t = c.motor_off_us;
    int64_t played_us = 0;
    uint64_t prev_pos = c.frozen_ticks;
    for (int i = 0; i < 25; ++i)
    {
        const int64_t on = t + 3000 + (i * 11) % 30000;
        const int64_t off = on + 3000000 + (i * 173) % 3000000;
        c = tp.run(on, off);
        REQUIRE(c.started);
        CHECK(c.froze);
        CHECK(c.frozen_ticks <= c.true_ticks);                 // never ahead of the tape
        CHECK(c.true_ticks - c.frozen_ticks <= 200);           // at most a callback latency behind
        CHECK(c.frozen_ticks >= prev_pos);
        CHECK(c.seed_ticks == prev_pos);                       // resumes exactly where it froze
        prev_pos = c.frozen_ticks;
        played_us += c.motor_off_us - c.tx_start_us;
        t = c.motor_off_us;
    }
    // Over 25 cycles the position advanced by what was played, and never more.
    CHECK(prev_pos - start_pos <= static_cast<uint64_t>(played_us));
    CHECK(prev_pos - start_pos + 25 * 200 >= static_cast<uint64_t>(played_us));
}

namespace
{
    struct DeviceThr
    {
        int64_t ts_us;
        uint64_t boundary;
    };

    // One row per RMT threshold, "[phase,ts_us,boundary,t0,t1,t2,t3,status]" (phase 0 =
    // end of the prefill, phase 1 = threshold callback), captured on the device.
    bool read_device_thr(const char *path, int64_t &prefill_end_us, std::vector<DeviceThr> &out)
    {
        std::FILE *f = std::fopen(path, "rb");
        if (f == nullptr)
            return false;
        char line[256];
        bool have_prefill = false;
        while (std::fgets(line, sizeof(line), f) != nullptr)
        {
            unsigned phase = 0, t0 = 0, t1 = 0, t2 = 0, t3 = 0, status = 0;
            long long ts = 0;
            unsigned long long boundary = 0;
            if (std::sscanf(line, "[%u,%lld,%llu,%u,%u,%u,%u,%u]", &phase, &ts, &boundary, &t0, &t1,
                            &t2, &t3, &status) != 8)
                continue;
            if (phase == 0 && !have_prefill)
            {
                prefill_end_us = ts;
                have_prefill = true;
            }
            else if (phase == 1)
                out.push_back({ static_cast<int64_t>(ts), static_cast<uint64_t>(boundary) });
        }
        std::fclose(f);
        return have_prefill && !out.empty();
    }
}

TEST_CASE("REAL DEVICE replay: the three-entry reference reproduces 495 measured Zorro thresholds")
{
    // The per-threshold timestamps and boundaries recorded on the FujiNet during a
    // complete Zorro load, replayed through the
    // production tracker on the real CAS. Skipped when either file is absent.
    const char *thr_path = std::getenv("CASSETTE_TIME_TESTS_THR_JSON");
    size_t filesize = 0;
    std::FILE *f = (thr_path != nullptr) ? open_zorro_cas(filesize) : nullptr;
    int64_t pf_end = 0;
    std::vector<DeviceThr> dev;
    if (f == nullptr || !read_device_thr(thr_path, pf_end, dev))
    {
        if (f != nullptr)
            std::fclose(f);
        MESSAGE("SKIPPED (set CASSETTE_TIME_TESTS_THR_JSON to a per-threshold capture of a full Zorro load)");
        return;
    }
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, 8, fx));
    std::fclose(f);
    const std::vector<uint16_t> values = real_run_values(fx, 0, 0);

    fskmodel::RmtPingPongModel m(values, 0, 0, pf_end);
    m.callback(2 * FSK_RMT_HALF_SYMBOLS, pf_end);            // prefill: the origin of the timeline
    REQUIRE(dev.size() >= 400);
    int64_t worst_behind = 0, best_behind = INT64_MAX, sum_behind = 0;
    size_t old_rule_ahead_10ms = 0;
    for (size_t n = 0; n < dev.size(); ++n)
    {
        const uint64_t raw = m.bt.pending_ticks;
        REQUIRE(raw == dev[n].boundary);                    // the model reproduces the device's boundary exactly
        m.callback(FSK_RMT_HALF_SYMBOLS, dev[n].ts_us);
        const uint64_t ref = m.log.ring[(m.log.count - 1u) % FSK_REF_RING].ticks;
        const int64_t truth = dev[n].ts_us - pf_end;         // where the pin was at the callback (1 tick = 1 us)
        const int64_t behind = truth - static_cast<int64_t>(ref);
        worst_behind = std::max(worst_behind, behind);
        best_behind = std::min(best_behind, behind);
        sum_behind += behind;
        if (static_cast<int64_t>(raw) - truth > 10000)
            ++old_rule_ahead_10ms;
    }
    CHECK(best_behind >= 0);                                 // never ahead of the pin
    CHECK(worst_behind <= 100);                              // behind by tens of us (callback latency), not ms
    CHECK(old_rule_ahead_10ms > 100);                        // the raw boundary was often >10 ms ahead
    std::printf("REAL DEVICE replay (%zu thresholds): reference behind the pin by %lld..%lld us, mean %.1f us; "
                "the raw boundary was >10 ms ahead in %zu of them\n",
                dev.size(), (long long)best_behind, (long long)worst_behind,
                (double)sum_behind / (double)dev.size(), old_rule_ahead_10ms);
}

// -----------------------------------------------------------------------------
// Tape time base (fsk_timebase_* + cas_fsk_inert_ticks): the tape has been running
// since MOTOR ON, so the start position is q0 + elapsed - but only across tape that
// carries no data. The compositions below mirror play_fsk_chunk() exactly:
//   want -> inert prefix -> q_start -> split -> locate.
// -----------------------------------------------------------------------------

namespace
{
    struct TbStart
    {
        uint64_t        want;
        uint64_t        inert;
        uint64_t        q_start;
        CasRunPosSplit  split;
        FskLocateResult loc;
    };

    TbStart timebase_start(const size_t *counts, size_t n, fsk_run_value_fn reader, void *ctx,
                           uint64_t irg_us, uint64_t q0, int64_t elapsed_us)
    {
        TbStart r{};
        r.want = fsk_timebase_wave_want(irg_us, q0, elapsed_us);
        r.inert = r.want > 0 ? cas_fsk_inert_ticks(counts, n, reader, ctx, r.want,
                                                   FSK_TIMEBASE_LOW_BUDGET_US)
                             : 0;
        r.q_start = fsk_timebase_q(irg_us, q0, elapsed_us, r.inert);
        r.split = cas_run_pos_split(irg_us, r.q_start);
        r.loc = r.split.wave_ticks > 0
                    ? cas_fsk_locate_ticks(counts, n, reader, ctx, r.split.wave_ticks)
                    : FskLocateResult{ 0, 0, 0, false };
        return r;
    }

    TbStart timebase_start(MemRun &run, uint64_t irg_us, uint64_t q0, int64_t elapsed_us)
    {
        const std::vector<size_t> c = run.counts();
        return timebase_start(c.data(), c.size(), mem_run_reader, &run, irg_us, q0, elapsed_us);
    }

    // A Zorro-shaped tape start: HIGH 700 us, a 100 us LOW blip, 19.66 s + 352.6 ms of HIGH
    // leader (zero-length LOWs in between), then 600-baud data.
    MemRun zorro_shape()
    {
        MemRun r;
        r.chunks.push_back({ 0, 7, 1, 65535, 0, 65535, 0, 65535, 0, 3526,
                             17, 17, 16, 16, 17, 17, 16, 17, 17, 16, 16, 17 });
        return r;
    }

    uint64_t oracle_ticks_real(FskRunFixture &fx, const FskLocateResult &r)
    {
        uint64_t t = 0;
        for (size_t c = 0; c <= r.chunk_index && c < fx.count; ++c)
        {
            const size_t vmax = (c == r.chunk_index) ? r.value_index : fx.value_counts[c];
            for (size_t v = 0; v < vmax; ++v)
                t += fsk_ticks_for_value(real_run_value_reader(&fx, c, v));
        }
        return t + r.skip_ticks;
    }

    constexpr uint64_t kZorroInert = 700 + 100 + 3 * 6553500ULL + 352600ULL; // 20,013,900 us
    constexpr uint64_t kIrgUs = 999000;
}

TEST_CASE("Time base: the inert prefix of a Zorro-shaped tape ends where the data starts")
{
    MemRun run = zorro_shape();
    const std::vector<size_t> c = run.counts();
    CHECK(kZorroInert == 20013900);
    // A generous limit: the scan stops at the first LOW that may be data (value 10).
    CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &run, UINT64_MAX / 2,
                              FSK_TIMEBASE_LOW_BUDGET_US) == kZorroInert);
    // The 100 us blip is inside the budget; the 1.7 ms start bit is not.
    CHECK(fsk_ticks_for_value(1) <= FSK_TIMEBASE_LOW_BUDGET_US);
    CHECK(fsk_ticks_for_value(17) > FSK_TIMEBASE_LOW_BUDGET_US);
}

TEST_CASE("Time base: the LOW budget is a hard, exact bound")
{
    // 0 (LOW, no time), HIGH 1 ms, LOW 300 us, HIGH 1 ms, LOW 100 us, HIGH 1 ms.
    MemRun run;
    run.chunks.push_back({ 0, 10, 3, 10, 1, 10 });
    std::vector<size_t> c = run.counts();
    // 300 us alone fits; the extra 100 us (400 us in total) does not: the scan ends where it starts.
    CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &run, UINT64_MAX / 2, 300) ==
          1000 + 300 + 1000);
    CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &run, UINT64_MAX / 2, 400) ==
          1000 + 300 + 1000 + 100 + 1000); // a bigger budget swallows it: inert to the end
    // A single LOW of 400 us is over a 300 us budget immediately.
    MemRun over;
    over.chunks.push_back({ 0, 10, 4, 10 });
    c = over.counts();
    CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &over, UINT64_MAX / 2, 300) == 1000);
    // Zero budget: any LOW at all is data.
    CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &over, UINT64_MAX / 2, 0) == 1000);
}

TEST_CASE("Time base: an all-MARK run is inert throughout")
{
    MemRun run; // Alien-style (0, N) values: no LOW time at all
    run.chunks.push_back({ 0, 65535, 0, 65535 });
    const std::vector<size_t> c = run.counts();
    CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &run, UINT64_MAX / 2, 300) ==
          2 * 6553500ULL);
}

TEST_CASE("Time base: the inert scan is cut by the limit and stays cheap")
{
    struct Counting
    {
        size_t calls = 0;
    } ctr;
    const size_t n = 200000; // 200k alternating 0 / 1 unit values: HIGH 100 us at every odd index
    std::vector<size_t> counts{ n };
    auto reader = [](void *ctx, size_t, size_t v) -> uint16_t
    {
        static_cast<Counting *>(ctx)->calls++;
        return (v & 1) ? 1 : 0;
    };
    const uint64_t r = cas_fsk_inert_ticks(counts.data(), counts.size(),
                                           static_cast<fsk_run_value_fn>(reader), &ctr, 1000, 300);
    CHECK(r >= 1000);
    CHECK(ctr.calls <= 30); // ~10 HIGH values reach 1000 us, not 200k reads
}

TEST_CASE("Time base: elapsed inside the IRG leaves only the remaining IRG")
{
    MemRun run = zorro_shape();
    const TbStart s = timebase_start(run, kIrgUs, 0, 400000);
    CHECK(s.want == 0);
    CHECK(s.q_start == 400000);
    CHECK(s.split.in_irg);
    CHECK(s.split.irg_remaining_us == kIrgUs - 400000); // 599 ms of IRG still to play
    CHECK(s.split.wave_ticks == 0);
}

TEST_CASE("Time base: elapsed exactly at the end of the IRG starts the waveform at tick 0")
{
    MemRun run = zorro_shape();
    const TbStart s = timebase_start(run, kIrgUs, 0, static_cast<int64_t>(kIrgUs));
    CHECK(s.want == 0);
    CHECK(s.q_start == kIrgUs);
    CHECK_FALSE(s.split.in_irg);
    CHECK(s.split.irg_remaining_us == 0);
    CHECK(s.split.wave_ticks == 0);
    // One microsecond earlier the IRG is still running.
    const TbStart e = timebase_start(run, kIrgUs, 0, static_cast<int64_t>(kIrgUs) - 1);
    CHECK(e.split.in_irg);
    CHECK(e.split.irg_remaining_us == 1);
}

TEST_CASE("Time base: elapsed inside the waveform counts the IRG once and lands with the resume machinery")
{
    MemRun run = zorro_shape();
    const int64_t elapsed = 5000000;
    const TbStart s = timebase_start(run, kIrgUs, 0, elapsed);
    CHECK(s.q_start == 5000000);                    // q = elapsed: the IRG is inside it, not added again
    CHECK_FALSE(s.split.in_irg);
    CHECK(s.split.wave_ticks == 5000000 - kIrgUs);  // 4,001,000 ticks already played
    // The landing is the very same locate a resume of that position performs: inside the
    // long HIGH leader (value 3, which starts 800 ticks in), never replaying it.
    CHECK(s.loc.value_index == 3);
    CHECK(s.loc.skip_ticks == 4001000 - 800);
    CHECK(oracle_ticks(run, s.loc) == s.split.wave_ticks);
    // ... and a MOTOR OFF right here freezes at exactly that q; the resume of it lands identically.
    const CasRunPosSplit again = cas_run_pos_split(kIrgUs, kIrgUs + s.split.wave_ticks);
    CHECK(again.wave_ticks == s.split.wave_ticks);
    CHECK_FALSE(again.in_irg);
}

TEST_CASE("Time base: the recorded blip is skipped once the tape has passed it, never later data")
{
    MemRun run = zorro_shape();
    // Just past the blip (700 + 100 us): the LOW blip is behind us, we land in the leader.
    TbStart s = timebase_start(run, kIrgUs, 0, static_cast<int64_t>(kIrgUs + 800));
    CHECK(s.split.wave_ticks == 800);
    CHECK(s.loc.value_index == 3);
    CHECK(s.loc.skip_ticks == 0);
    // Well past it, still leader: 9.35 s of tape = 8.351 s of waveform, i.e. inside the second
    // 6.5535 s HIGH value (value 3 covers ticks 800 .. 6,554,300, value 5 the next 6.5535 s).
    s = timebase_start(run, kIrgUs, 0, 9350000);
    CHECK(s.q_start == 9350000);
    CHECK(s.loc.value_index == 5);
    // Whatever the delay, the position never goes beyond the data start (20.0139 s of waveform).
    for (int64_t elapsed : { 25000000LL, 60000000LL, 3600000000LL })
    {
        s = timebase_start(run, kIrgUs, 0, elapsed);
        CHECK(s.split.wave_ticks == kZorroInert);
        CHECK(s.q_start == kIrgUs + kZorroInert);
        CHECK(s.loc.value_index == 10); // the first 1.7 ms LOW: the boot record's start bit
        CHECK(s.loc.skip_ticks == 0);
    }
}

TEST_CASE("Time base: a run with data early is never advanced past it")
{
    MemRun run; // 2 s of leader, then data
    run.chunks.push_back({ 0, 20000, 17, 17, 16, 16, 17, 17 });
    for (int64_t elapsed : { 1500000LL, 8000000LL, 600000000LL })
    {
        const TbStart s = timebase_start(run, kIrgUs, 0, elapsed);
        CHECK(s.split.wave_ticks <= 2000000);
        if (elapsed >= 8000000)
        {
            CHECK(s.split.wave_ticks == 2000000);
            CHECK(s.loc.value_index == 2); // lands exactly on the first data LOW
            CHECK(s.loc.skip_ticks == 0);
        }
    }
    // Data from the very first value: nothing at all may be skipped (only the IRG passes).
    MemRun immediate;
    immediate.chunks.push_back({ 17, 17, 16, 16 });
    const TbStart s = timebase_start(immediate, kIrgUs, 0, 30000000);
    CHECK(s.split.wave_ticks == 0);
    CHECK(s.q_start == kIrgUs);
}

TEST_CASE("Time base: near and past the end of an inert run")
{
    MemRun run; // 3 s of pure MARK
    run.chunks.push_back({ 0, 30000 });
    const uint64_t total = 3000000;
    // Well inside the run.
    TbStart s = timebase_start(run, kIrgUs, 0, static_cast<int64_t>(kIrgUs + 1000000));
    CHECK(s.split.wave_ticks == 1000000);
    CHECK_FALSE(s.loc.at_end);
    // One tick before the end: still a (tiny) tail to play.
    s = timebase_start(run, kIrgUs, 0, static_cast<int64_t>(kIrgUs + total - 1));
    CHECK(s.split.wave_ticks == total - 1);
    CHECK_FALSE(s.loc.at_end);
    // Exactly at and beyond the end: the whole run has passed (it carried no data), and the
    // position never goes past its end however long the preparation took.
    s = timebase_start(run, kIrgUs, 0, static_cast<int64_t>(kIrgUs + total));
    CHECK(s.split.wave_ticks == total);
    CHECK(s.loc.at_end);
    s = timebase_start(run, kIrgUs, 0, 3600000000LL);
    CHECK(s.split.wave_ticks == total);
    CHECK(s.q_start == kIrgUs + total);
    CHECK(s.loc.at_end);
}

TEST_CASE("Time base: a resume or a rewind is never touched")
{
    // The dispatch of a frozen (R, Q) - which is what a MOTOR resume and a rewind produce -
    // is `resumed`: the time base does not apply, whatever the other conditions say.
    for (int first : { 0, 1 })
        for (int stamp : { 0, 1 })
            for (int loaded : { 0, 1 })
                CHECK_FALSE(fsk_timebase_applies(true, first != 0, stamp != 0, loaded != 0));
    // It needs all of: not resumed, first dispatch at the start of the tape, a MOTOR ON stamp,
    // a resident run.
    CHECK(fsk_timebase_applies(false, true, true, true));
    CHECK_FALSE(fsk_timebase_applies(false, false, true, true));
    CHECK_FALSE(fsk_timebase_applies(false, true, false, true));
    CHECK_FALSE(fsk_timebase_applies(false, true, true, false));
    // Without elapsed time the position is exactly the one the dispatch asked for.
    for (uint64_t q0 : { (uint64_t)0, (uint64_t)500000, kIrgUs, kIrgUs + 12345, (uint64_t)633610000 })
    {
        CHECK(fsk_timebase_q(kIrgUs, q0, 0, UINT64_MAX / 2) == q0);
        CHECK(fsk_timebase_q(kIrgUs, q0, -5, UINT64_MAX / 2) == q0);
        CHECK(fsk_timebase_wave_want(kIrgUs, q0, 0) == 0);
    }
}

TEST_CASE("Time base: MOTOR OFF during the preparation freezes the advanced position, and it resumes there")
{
    MemRun run = zorro_shape();
    for (int64_t elapsed : { 100000LL, 999000LL, 999001LL, 4000000LL, 9350000LL, 40000000LL })
    {
        const TbStart s = timebase_start(run, kIrgUs, 0, elapsed);
        // What play_fsk_chunk freezes: irg_us + p0 once past the IRG (its post-preload check),
        // or q_start + (IRG time already held) inside it (its IRG-hold check, 0 held here).
        const uint64_t frozen = s.split.in_irg ? s.q_start : kIrgUs + s.split.wave_ticks;
        CHECK(frozen == s.q_start);
        // The next MOTOR ON resumes that (R, Q) through the very same split: nothing shifts.
        const CasRunPosSplit resumed = cas_run_pos_split(kIrgUs, frozen);
        CHECK(resumed.in_irg == s.split.in_irg);
        CHECK(resumed.irg_remaining_us == s.split.irg_remaining_us);
        CHECK(resumed.wave_ticks == s.split.wave_ticks);
    }
}

TEST_CASE("REAL CAS time base: Zorro's start is a 100 us blip and 20 s of leader, none of it data")
{
    size_t filesize = 0;
    std::FILE *f = require_real_cas(filesize);
    if (f == nullptr)
        return;
    FskRunFixture fx;
    REQUIRE(scan_fsk_run(f, filesize, 8, fx));
    std::fclose(f);

    CHECK(cas_fsk_inert_ticks(fx.value_counts, fx.count, real_run_value_reader, &fx,
                              UINT64_MAX / 2, FSK_TIMEBASE_LOW_BUDGET_US) == kZorroInert);

    // v7 failed with the waveform starting at MOTOR_ON + 10.19 s and passed with 9.70 s; the time
    // base makes that irrelevant: whatever the preload takes, the blip is behind the tape.
    for (int64_t elapsed : { 6330000LL, 9350000LL, 9700000LL, 9800000LL, 10190000LL, 10350000LL,
                             12000000LL })
    {
        const TbStart s = timebase_start(fx.value_counts, fx.count, real_run_value_reader, &fx,
                                         kIrgUs, 0, elapsed);
        CHECK(s.q_start == static_cast<uint64_t>(elapsed)); // IRG counted once, nothing clamped
        CHECK(s.split.wave_ticks == static_cast<uint64_t>(elapsed) - kIrgUs);
        CHECK(s.split.wave_ticks > 800);                     // past the 700 us HIGH + 100 us LOW blip
        // Inside a 6.5535 s HIGH leader value: value 3 up to tick 6,554,300, value 5 after it.
        CHECK(s.loc.value_index == (s.split.wave_ticks < 6554300 ? 3u : 5u));
        CHECK(oracle_ticks_real(fx, s.loc) == s.split.wave_ticks);
    }
    // A 60 s preparation still never skips the boot record.
    const TbStart late = timebase_start(fx.value_counts, fx.count, real_run_value_reader, &fx,
                                        kIrgUs, 0, 60000000);
    CHECK(late.split.wave_ticks == kZorroInert);
    CHECK(late.loc.chunk_index == 0);
    CHECK(late.loc.value_index == 10);
    CHECK(late.loc.skip_ticks == 0);
}

namespace
{
    // The first joined "fsk " run of a CAS file, as plain value arrays (one per chunk).
    bool corpus_first_run(const std::string &path, std::vector<std::vector<uint16_t>> &out)
    {
        std::FILE *f = std::fopen(path.c_str(), "rb");
        if (f == nullptr)
            return false;
        std::vector<uint8_t> raw;
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            raw.insert(raw.end(), buf, buf + n);
        std::fclose(f);
        size_t off = 0;
        bool in_run = false;
        while (off + 8 <= raw.size())
        {
            const bool is_fsk = std::memcmp(&raw[off], "fsk ", 4) == 0;
            const uint16_t len = static_cast<uint16_t>(raw[off + 4] | (raw[off + 5] << 8));
            const uint16_t irg = static_cast<uint16_t>(raw[off + 6] | (raw[off + 7] << 8));
            const size_t body = std::min<size_t>(len, raw.size() - off - 8);
            if (is_fsk && (!in_run || irg == 0))
            {
                in_run = true;
                std::vector<uint16_t> v;
                for (size_t i = 0; i + 1 < body; i += 2)
                    v.push_back(static_cast<uint16_t>(raw[off + 8 + i] | (raw[off + 8 + i + 1] << 8)));
                out.push_back(v);
            }
            else if (in_run)
                break;
            off += 8 + len;
        }
        return !out.empty();
    }

    // Independent oracle: waveform tick where the cumulative LOW time first exceeds `budget`
    // (expanded value by value, no early exit tricks), capped at the run total.
    uint64_t oracle_inert(const std::vector<std::vector<uint16_t>> &chunks, uint64_t budget)
    {
        uint64_t t = 0, low = 0;
        for (const auto &ch : chunks)
            for (size_t v = 0; v < ch.size(); ++v)
            {
                const uint64_t d = static_cast<uint64_t>(ch[v]) * 100;
                if ((v & 1) == 0 && d > 0)
                {
                    if (low + d > budget)
                        return t;
                    low += d;
                }
                t += d;
            }
        return t;
    }
}

TEST_CASE("Time base: every CAS of the reviewer corpus keeps its data (optional corpus directory)")
{
    // CASSETTE_TIME_TESTS_CORPUS_DIR points at a folder with the .cas files the reviewer used
    // (Zorro, Mirax Force, Alien Ambush ...). Absent, the test only reports that it did not run.
    const char *dir = std::getenv("CASSETTE_TIME_TESTS_CORPUS_DIR");
    if (dir == nullptr)
    {
        MESSAGE("SKIPPED (CASSETTE_TIME_TESTS_CORPUS_DIR not set) - no corpus file was checked");
        return;
    }
    const char *names[] = {
        "turbo_software_zorro.cas",        "turbo_software_zorro_v2.cas",
        "turbo_software_mirax_force.cas",  "turbo_software_bruce_lee.cas",
        "turbo_software_missile_command.cas", "tt_international.cas",
        "tt_river_raid.cas",               "international_karate.cas",
        "NTSC_ALIEN_AMBUSH_TURBO_SOFTWARE.cas", "PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas",
        "NTSC_BRUCE_LEE_TURBO_SOFTWARE_FUJINET.cas", "NTSC_HERO_TURBO_SOFTWARE_FUJINET.cas",
        "NTSC_HIJACK_TURBO_SOFTWARE_FUJINET.cas", "NTSC_MINER_2049ER_TURBO_SOFTWARE_FUJINET.cas",
        "NTSC_MONTEZUMA_AGAIN_TURBO_SOFTWARE_FUJINET.cas",
        "NTSC_POPEYE_ARCADE_5200_TURBO_SOFTWARE_FUJINET.cas",
    };
    int checked = 0;
    for (const char *name : names)
    {
        std::vector<std::vector<uint16_t>> chunks;
        if (!corpus_first_run(std::string(dir) + "/" + name, chunks))
            continue;
        ++checked;
        MemRun run;
        run.chunks = chunks;
        const std::vector<size_t> c = run.counts();
        const uint64_t expect = oracle_inert(chunks, FSK_TIMEBASE_LOW_BUDGET_US);
        INFO("file: ", name);
        CHECK(cas_fsk_inert_ticks(c.data(), c.size(), mem_run_reader, &run, UINT64_MAX / 2,
                                  FSK_TIMEBASE_LOW_BUDGET_US) == expect);
        // However long the preparation, the start never passes the first LOW that may be data.
        for (int64_t elapsed : { 1000000LL, 6330000LL, 9800000LL, 30000000LL, 3600000000LL })
        {
            const TbStart s = timebase_start(run, 999000, 0, elapsed);
            CHECK(s.split.wave_ticks <= expect);
            CHECK(s.q_start <= 999000 + expect);
        }
    }
    MESSAGE("corpus files checked: ", checked);
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
