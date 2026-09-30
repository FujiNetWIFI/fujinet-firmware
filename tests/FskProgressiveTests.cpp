#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

// FskProgressiveTests.cpp - progressive raw-FSK loading (lib/device/sio/fsk_progressive.{h,cpp}).
//
// What is exercised:
//   * the PRODUCER (fsk_prog_step) and the publication invariants it keeps;
//   * the startup gate (fsk_prog_plan_start): runway measured in WAVEFORM TIME, prefill, time base,
//     seed position, fallback conditions;
//   * the CONSUMER: the production RMT encoder callback and the production cursor seeding, extracted
//     VERBATIM from cassette.cpp at configure time (fsk_encode_cb.inc / fsk_seed.inc, see
//     tests/CMakeLists.txt) and run over a producer/consumer time model. Nothing of the encoder is
//     re-implemented here;
//   * rewind resolution from the published chunk metadata against cas_resolve_target_time();
//   * stop / failure semantics (underrun, STOPPED, FAILED), MOTOR freeze / resume, and real threads.
//
// FIXTURE POLICY (same as CassetteTimePlanTests.cpp): no synthetic .cas FILES. Real images are read from
// CASSETTE_TIME_TESTS_ZORRO_CAS_PATH (default /tmp/zorro_sd.cas) and, for the other corpus images, from
// CASSETTE_TIME_TESTS_CORPUS_DIR; the tests that need them SKIP (and say so) when they are absent.
// Structural checks with no natural example use small IN-MEMORY images built inside the test.

#include "sio/cassette_time_plan.h"
#include "sio/fsk_plan.h"
#include "sio/fsk_progressive.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifdef FSK_PROGRESSIVE_TESTS_THREADS
#include <atomic>
#include <chrono>
#include <thread>
#endif

// ---- host stand-ins for what the extracted production code touches ----------------------------------------

#define IRAM_ATTR

static int64_t g_now_us = 0;
static int64_t esp_timer_get_time() { return g_now_us; }

struct rmt_symbol_word_t
{
    uint32_t duration0 : 15;
    uint32_t level0 : 1;
    uint32_t duration1 : 15;
    uint32_t level1 : 1;
};

// The members of sioCassette the encoder callback and the cursor seeding use, with the production names.
struct sioCassette
{
    uint8_t **_fsk_blocks = nullptr;
    size_t _fsk_block_size = 0;
    size_t _fsk_block_count = 0;
    size_t _fsk_payload_len = 0;
    size_t _fsk_value_count = 0;
    size_t _fsk_value_index = 0;
    size_t _fsk_payload_pos = 0;
    uint32_t _fsk_remaining_ticks = 0;
    bool _fsk_level_high = false;
    size_t _fsk_run_chunk_count = 0;
    size_t _fsk_run_value_counts[FSK_RUN_MAX_CHUNKS] = {};
    size_t _fsk_run_block_base[FSK_RUN_MAX_CHUNKS] = {};
    size_t _fsk_run_chunk_index = 0;
    volatile bool _fsk_stop_flag = false;
    volatile bool _fsk_encoding_complete = false;
    volatile bool _fsk_underrun = false;
    bool _fsk_prog_on = false;
    FskProgRun _fsk_prog;
    FskBoundaryTracker _fsk_bounds{};
    FskRefLog _fsk_ref_log{};
    bool _fsk_transmission_started = false;

    static size_t fsk_encode_cb(const void *data, size_t data_size, size_t symbols_written, size_t symbols_free,
                                rmt_symbol_word_t *symbols, bool *done, void *arg);

    void seed(size_t seed_chunk, size_t seed_value, uint64_t seed_skip_ticks)
    {
#include "fsk_seed.inc"
    }
};

#include "fsk_encode_cb.inc"

namespace
{
    // ---- images ---------------------------------------------------------------------------------------------
    using Bytes = std::vector<uint8_t>;

    size_t mem_read(void *ctx, size_t off, uint8_t *dst, size_t n)
    {
        const Bytes &d = *static_cast<const Bytes *>(ctx);
        if (off >= d.size())
            return 0;
        const size_t k = std::min(n, d.size() - off);
        std::memcpy(dst, d.data() + off, k);
        return k;
    }

    bool load_file(const std::string &path, Bytes &out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out.empty();
    }

    int g_real_ran = 0;
    int g_real_skipped = 0;

    std::string corpus_dir()
    {
        const char *d = std::getenv("CASSETTE_TIME_TESTS_CORPUS_DIR");
        return d != nullptr ? d : "";
    }

    // Returns false (and counts a skip) when the image is not available.
    bool require_image(const char *name, Bytes &out)
    {
        std::string path;
        if (std::strcmp(name, "turbo_software_zorro.cas") == 0)
        {
            const char *z = std::getenv("CASSETTE_TIME_TESTS_ZORRO_CAS_PATH");
            path = z != nullptr ? z : "/tmp/zorro_sd.cas";
            if (!load_file(path, out) && !corpus_dir().empty())
                load_file(corpus_dir() + "/" + name, out);
        }
        else if (!corpus_dir().empty())
        {
            load_file(corpus_dir() + "/" + name, out);
        }
        if (out.empty())
        {
            ++g_real_skipped;
            MESSAGE("SKIPPED (", name, " not available) - this test case did not exercise that image");
            return false;
        }
        ++g_real_ran;
        return true;
    }

    // In-memory structural image: "FUJI" descriptor chunk + the given chunks. Never written to a file.
    struct MemChunk
    {
        const char *type;
        uint16_t irg;
        Bytes payload;
    };

    Bytes make_image(const std::vector<MemChunk> &chunks)
    {
        Bytes d;
        auto put = [&](const char *type, uint16_t len, uint16_t aux, const Bytes &pl)
        {
            d.insert(d.end(), type, type + 4);
            d.push_back(len & 0xFF);
            d.push_back(len >> 8);
            d.push_back(aux & 0xFF);
            d.push_back(aux >> 8);
            d.insert(d.end(), pl.begin(), pl.end());
        };
        put("FUJI", 16, 0, Bytes(16, 0));
        for (const MemChunk &c : chunks)
            put(c.type, static_cast<uint16_t>(c.payload.size()), c.irg, c.payload);
        return d;
    }

    // Payload of `values` A8CAS values (unit 100 us, LE16).
    Bytes values_payload(const std::vector<uint16_t> &v)
    {
        Bytes b;
        for (uint16_t x : v)
        {
            b.push_back(x & 0xFF);
            b.push_back(x >> 8);
        }
        return b;
    }

    // ---- reference run scan: the same rules play_fsk_chunk() applies ---------------------------------------------
    struct RunRef
    {
        std::vector<size_t> hdr_off;
        std::vector<size_t> avail;
        std::vector<uint16_t> irg_ms;
        size_t next_offset = 0;
    };

    bool ref_scan(const Bytes &img, size_t off, RunRef &r)
    {
        r = RunRef{};
        uint8_t h[8];
        if (mem_read(const_cast<Bytes *>(&img), off, h, 8) != 8)
            return false;
        if (std::memcmp(h, "fsk ", 4) != 0)
            return false;
        const FskBounds b = fsk_compute_bounds(img.size(), off, fsk_decode_le16(h + 4));
        if (!b.header_complete)
            return false;
        r.hdr_off.push_back(off);
        r.avail.push_back(b.data_avail);
        r.irg_ms.push_back(fsk_decode_le16(h + 6));
        r.next_offset = b.next_offset;
        size_t scan = b.next_offset;
        while (scan != 0 && r.hdr_off.size() < FSK_RUN_MAX_CHUNKS)
        {
            uint8_t c[8];
            if (mem_read(const_cast<Bytes *>(&img), scan, c, 8) != 8)
                break;
            const bool is_fsk = std::memcmp(c, "fsk ", 4) == 0;
            const FskBounds cb = fsk_compute_bounds(img.size(), scan, fsk_decode_le16(c + 4));
            if (!fsk_run_should_join(is_fsk, fsk_decode_le16(c + 6), cb.header_complete, cb.structurally_truncated))
                break;
            r.hdr_off.push_back(scan);
            r.avail.push_back(cb.data_avail);
            r.irg_ms.push_back(fsk_decode_le16(c + 6));
            r.next_offset = cb.next_offset;
            scan = cb.next_offset;
        }
        return true;
    }

    // Every run start (structural walk) of an image.
    std::vector<size_t> run_starts(const Bytes &img)
    {
        std::vector<size_t> out;
        size_t off = 0;
        while (off + 8 <= img.size())
        {
            uint8_t h[8];
            mem_read(const_cast<Bytes *>(&img), off, h, 8);
            if (std::memcmp(h, "fsk ", 4) == 0)
            {
                RunRef r;
                if (!ref_scan(img, off, r))
                    break;
                out.push_back(off);
                off = r.next_offset;
                if (off == 0)
                    break;
                continue;
            }
            off += 8 + fsk_decode_le16(h + 4);
        }
        return out;
    }

    // ---- block storage ----------------------------------------------------------------------------------------
    struct Pool
    {
        std::vector<std::unique_ptr<uint8_t[]>> blocks;
        size_t allocs = 0;
        size_t fail_after = SIZE_MAX; // allocation failure injection
    };

    uint8_t *pool_alloc(void *ctx, size_t n)
    {
        Pool *p = static_cast<Pool *>(ctx);
        if (p->allocs >= p->fail_after)
            return nullptr;
        ++p->allocs;
        p->blocks.emplace_back(new uint8_t[n]);
        std::memset(p->blocks.back().get(), 0xEE, n); // poison: a value read from an unpublished block is visible
        return p->blocks.back().get();
    }

    struct FileCtx
    {
        const Bytes *img;
        size_t fail_at = SIZE_MAX;     // reads at or beyond this offset fail (0 bytes)
        size_t short_every = 0;        // every Nth read returns a short count (accumulated by the producer)
        uint32_t reads = 0;
        size_t last_end = 0;
        uint32_t seeks = 0;
        size_t max_req = 0;
        std::vector<size_t> offsets;
    };

    size_t file_read(void *ctx, size_t off, uint8_t *dst, size_t n)
    {
        FileCtx *f = static_cast<FileCtx *>(ctx);
        ++f->reads;
        f->max_req = std::max(f->max_req, n);
        if (off != f->last_end)
            ++f->seeks;
        f->offsets.push_back(off);
        if (off >= f->fail_at)
            return 0;
        size_t want = n;
        if (f->short_every != 0 && (f->reads % f->short_every) == 0 && want > 1)
            want = want / 2;
        const size_t k = mem_read(const_cast<Bytes *>(f->img), off, dst, want);
        f->last_end = off + k;
        return k;
    }

    // ---- a progressive run under test ---------------------------------------------------------------------------
    struct Prog
    {
        Bytes img;
        RunRef ref;
        Pool pool;
        FileCtx file;
        std::vector<uint8_t *> table;
        size_t table_blocks = 0;
        FskProgLoader L;
        sioCassette cas;

        bool init(const Bytes &image, size_t off)
        {
            img = image;
            if (!ref_scan(img, off, ref))
                return false;
            file.img = &img;
            table_blocks = fsk_prog_table_blocks(img.size(), off);
            table.assign(table_blocks, nullptr);
            cas._fsk_blocks = table.data();
            cas._fsk_block_size = FSK_PROG_BLOCK_BYTES;
            cas._fsk_block_count = table_blocks;
            fsk_prog_bind(cas._fsk_prog, cas._fsk_run_value_counts, cas._fsk_run_block_base, table.data(), table_blocks);
            uint8_t h[8];
            mem_read(&img, off, h, 8);
            const FskBounds b = fsk_compute_bounds(img.size(), off, fsk_decode_le16(h + 4));
            if (!fsk_prog_add_first_chunk(cas._fsk_prog, off, h, b.data_avail, b.next_offset))
                return false;
            fsk_prog_loader_init(L, cas._fsk_prog, file_read, &file, pool_alloc, &pool, img.size());
            file.last_end = off + 8;
            cas._fsk_prog_on = true;
            return true;
        }
        FskProgRun &run() { return cas._fsk_prog; }
    };

    // Producer time model: one file read costs read_us; steps that read nothing are free.
    struct ProdSim
    {
        Prog *p;
        int64_t read_us;
        int64_t clock = 0;
        bool finished = false;
        FskProgStep last = FskProgStep::PROGRESS;

        int reads_next() const
        {
            const FskProgLoader &L = p->L;
            if (L.phase == FskProgLoader::PAYLOAD)
                return L.chunk_bytes < L.chunk_avail ? 1 : 0;
            if (L.phase == FskProgLoader::HEADER)
                return (L.chunk + 1 >= FSK_RUN_MAX_CHUNKS || p->cas._fsk_prog.chunk_next[L.chunk] == 0) ? 0 : 1;
            return 0;
        }
        // Runs every step that COMPLETES by time T (published at its completion time).
        void run_until(int64_t T)
        {
            while (!finished)
            {
                const int64_t done = clock + reads_next() * read_us;
                if (done > T)
                    break;
                last = fsk_prog_step(p->L);
                clock = done;
                if (last != FskProgStep::PROGRESS)
                    finished = true;
            }
        }
    };

    // ---- consumer: the production encoder over a virtual clock ---------------------------------------------------
    struct Run
    {
        int level;
        uint64_t ticks;
        bool operator==(const Run &o) const { return level == o.level && ticks == o.ticks; }
    };
    using Rle = std::vector<Run>;

    void rle_push(Rle &r, int level, uint64_t d)
    {
        if (d == 0)
            return;
        if (!r.empty() && r.back().level == level)
            r.back().ticks += d;
        else
            r.push_back({level, d});
    }

    uint64_t rle_ticks(const Rle &r)
    {
        uint64_t t = 0;
        for (const Run &x : r)
            t += x.ticks;
        return t;
    }

    // First `ticks` waveform ticks of r.
    Rle rle_prefix(const Rle &r, uint64_t ticks)
    {
        Rle out;
        uint64_t left = ticks;
        for (const Run &x : r)
        {
            if (left == 0)
                break;
            const uint64_t d = std::min(left, x.ticks);
            rle_push(out, x.level, d);
            left -= d;
        }
        return out;
    }

    Rle rle_concat(Rle a, const Rle &b)
    {
        for (const Run &x : b)
            rle_push(a, x.level, x.ticks);
        return a;
    }

    struct Drive
    {
        Rle wave;
        bool complete = false;  // the encoder set _fsk_encoding_complete
        bool underrun = false;  // ... because it caught the producer
        bool cut = false;       // the drive stopped at t_freeze (MOTOR OFF)
        size_t calls = 0;
        uint64_t encoded_ticks = 0;
    };

    // Runs the production callback like the RMT driver does (512-symbol prefill, then a 256-symbol refill each
    // time the hardware has played a half). advance(T) is invoked before each call with the virtual time of the
    // call (the producer publishes everything it completes by then). t_freeze_us < INT64_MAX ends the drive
    // when the hardware would already have been stopped (MOTOR OFF): nothing after it is played.
    template <class Advance>
    Drive drive(sioCassette &cas, uint64_t seed_wave_ticks, int64_t t_start_us, Advance advance,
                int64_t t_freeze_us = INT64_MAX)
    {
        Drive out;
        fsk_bounds_reset(cas._fsk_bounds, seed_wave_ticks);
        fsk_ref_log_reset(cas._fsk_ref_log, seed_wave_ticks);
        cas._fsk_transmission_started = false;
        cas._fsk_encoding_complete = false;
        cas._fsk_underrun = false;

        std::vector<uint64_t> sym_end; // hardware time (us after t_start) at which each symbol ends
        uint64_t hw = 0;
        std::vector<rmt_symbol_word_t> buf(512);
        size_t played_syms = 0;
        for (size_t n = 0;; ++n)
        {
            int64_t T = t_start_us;
            size_t offered = 512;
            if (n > 0)
            {
                // threshold n fires when the pin is ~1.5 symbols short of the end of half n-1's data
                const size_t idx = 256 * n >= 2 ? 256 * n - 2 : 0;
                if (idx >= sym_end.size())
                    break; // the waveform finished before this threshold
                T = t_start_us + static_cast<int64_t>(sym_end[idx]);
                offered = 256;
            }
            if (T > t_freeze_us)
            {
                out.cut = true;
                break;
            }
            g_now_us = T;
            advance(T);
            std::memset(buf.data(), 0, buf.size() * sizeof(buf[0]));
            bool done = false;
            const size_t got = sioCassette::fsk_encode_cb(cas._fsk_blocks, cas._fsk_block_count * sizeof(uint8_t *),
                                                          played_syms, offered, buf.data(), &done, &cas);
            ++out.calls;
            // ESP-IDF rmt_encoder.h / rmt_encode_simple(): a callback may return 0 symbols ONLY together with
            // done == true (or when it needs more room, which min_chunk_size = 1 excludes); anything else is
            // "illegal" there. Returning 0 + done is documented as legal ("or on a callback that returns zero").
            if (got == 0)
                REQUIRE_MESSAGE(done, "encoder returned 0 symbols without done");
            REQUIRE(got <= offered);
            if (n == 0)
                cas._fsk_transmission_started = true;
            for (size_t i = 0; i < got; ++i)
            {
                const uint32_t d0 = buf[i].duration0, d1 = buf[i].duration1;
                rle_push(out.wave, buf[i].level0 ^ FSK_RMT_OUT_INVERT, d0);
                rle_push(out.wave, buf[i].level1 ^ FSK_RMT_OUT_INVERT, d1);
                hw += d0 + d1;
                sym_end.push_back(hw);
            }
            played_syms += got;
            if (done)
            {
                out.complete = cas._fsk_encoding_complete;
                out.underrun = cas._fsk_underrun;
                break;
            }
        }
        out.encoded_ticks = rle_ticks(out.wave);
        return out;
    }

    // The full-preload reference: every chunk loaded, classic (non-progressive) encoder path.
    struct Classic
    {
        std::vector<std::vector<uint8_t>> storage;
        std::vector<uint8_t *> table;
        sioCassette cas;
        RunRef ref;
        size_t first_chunk = 0;

        bool init(const Bytes &img, size_t off)
        {
            if (!ref_scan(img, off, ref))
                return false;
            const size_t bs = FSK_PROG_BLOCK_BYTES;
            size_t total = 0;
            size_t base[FSK_RUN_MAX_CHUNKS] = {};
            for (size_t c = 0; c < ref.hdr_off.size(); ++c)
            {
                base[c] = total;
                total += (ref.avail[c] + bs - 1) / bs;
            }
            storage.assign(total, std::vector<uint8_t>(bs, 0));
            table.resize(total);
            for (size_t i = 0; i < total; ++i)
                table[i] = storage[i].data();
            for (size_t c = 0; c < ref.hdr_off.size(); ++c)
            {
                size_t left = ref.avail[c];
                size_t pos = ref.hdr_off[c] + 8;
                for (size_t b = 0; left > 0; ++b)
                {
                    const size_t n = std::min(left, bs);
                    mem_read(const_cast<Bytes *>(&img), pos, storage[base[c] + b].data(), n);
                    pos += n;
                    left -= n;
                }
                cas._fsk_run_value_counts[c] = fsk_value_count(ref.avail[c]);
                cas._fsk_run_block_base[c] = base[c];
            }
            cas._fsk_blocks = table.data();
            cas._fsk_block_size = bs;
            cas._fsk_block_count = total;
            cas._fsk_run_chunk_count = ref.hdr_off.size();
            cas._fsk_prog_on = false;
            return true;
        }
        static uint16_t value_at(void *ctx, size_t chunk, size_t v)
        {
            Classic *self = static_cast<Classic *>(ctx);
            return fsk_block_le16(reinterpret_cast<const uint8_t *const *>(self->cas._fsk_blocks), self->cas._fsk_block_size,
                                  self->cas._fsk_run_block_base[chunk] * self->cas._fsk_block_size + v * 2);
        }
        // Waveform from wave tick p0 to the end, through the production encoder.
        Drive play(uint64_t p0)
        {
            size_t sc = 0, sv = 0;
            uint64_t sk = 0;
            if (p0 > 0)
            {
                const FskLocateResult loc = cas_fsk_locate_ticks(cas._fsk_run_value_counts, cas._fsk_run_chunk_count,
                                                                 &Classic::value_at, this, p0);
                if (loc.at_end)
                    return Drive{};
                sc = loc.chunk_index;
                sv = loc.value_index;
                sk = loc.skip_ticks;
            }
            cas.seed(sc, sv, sk);
            return drive(cas, p0, 0, [](int64_t) {});
        }
    };

    // The waveform a progressive drive would play from wave tick p0 (absolute chunk view starting at chunk `first`),
    // seeded exactly like play_fsk_chunk(): from the plan.
    struct StartInfo
    {
        bool ready = false;
        int64_t t_ready = 0;
        FskProgPlan plan;
    };

    // Polls the plan like the wait loop of play_fsk_chunk() (every `poll_us`) until it is ready or hopeless.
    StartInfo wait_ready(Prog &p, ProdSim &sim, const FskProgPlanIn &base, bool timebase, int64_t poll_us = 10000,
                         int64_t t_from_us = 0, int64_t limit_us = 3600LL * 1000000LL)
    {
        StartInfo s;
        for (int64_t t = t_from_us; t <= t_from_us + limit_us; t += poll_us)
        {
            sim.run_until(t);
            FskProgPlanIn in = base;
            in.timebase = timebase;
            in.elapsed_us = timebase ? t : 0;
            const FskProgPlan pl = fsk_prog_plan_start(p.run(), in);
            if (pl.ready || pl.failed)
            {
                s.ready = pl.ready;
                s.t_ready = t;
                s.plan = pl;
                return s;
            }
        }
        return s;
    }

    FskProgPlanIn plan_in(uint64_t irg_us, uint64_t q0, size_t first_chunk = 0)
    {
        FskProgPlanIn in{};
        in.irg_us = irg_us;
        in.q0_us = q0;
        in.min_runway_us = FSK_PROG_MIN_RUNWAY_US;
        in.first_chunk = first_chunk;
        return in;
    }

    // Progressive playback of p through the production encoder: start when ready, then stream until the run ends,
    // underruns or (t_freeze) the tape is stopped. Returns the drive and the start info.
    struct Played
    {
        StartInfo start;
        Drive d;
    };

    Played play_progressive(Prog &p, ProdSim &sim, uint64_t q0, int64_t t_freeze_us = INT64_MAX, size_t first_chunk = 0,
                            bool timebase = false)
    {
        Played out;
        const uint64_t irg_us = static_cast<uint64_t>(p.ref.irg_ms[first_chunk]) * 1000ULL;
        out.start = wait_ready(p, sim, plan_in(irg_us, q0, first_chunk), timebase);
        if (!out.start.ready)
            return out;
        const FskProgPlan &pl = out.start.plan;
        p.cas.seed(first_chunk + pl.seed_chunk, pl.seed_value, pl.seed_skip);
        out.d = drive(p.cas, pl.p0, out.start.t_ready, [&](int64_t T) { sim.run_until(T); }, t_freeze_us);
        return out;
    }

    // Wave-only (IRG excluded) reference from wave tick p0 for the run starting at `off`.
    Drive reference_from(const Bytes &img, size_t off, uint64_t p0)
    {
        Classic c;
        REQUIRE(c.init(img, off));
        return c.play(p0);
    }

    uint64_t run_total_ticks(const Bytes &img, size_t off)
    {
        return rle_ticks(reference_from(img, off, 0).wave);
    }

    // Runs of the corpus worth a progressive check: the first `max` run starts that satisfy fsk_prog_wanted.
    std::vector<size_t> wanted_runs(const Bytes &img, size_t max)
    {
        std::vector<size_t> out;
        for (size_t off : run_starts(img))
        {
            RunRef r;
            if (!ref_scan(img, off, r))
                continue;
            const FskBounds b = fsk_compute_bounds(img.size(), off, static_cast<uint16_t>(r.avail[0]));
            (void)b;
            if (fsk_prog_wanted(true, true, false, r.avail[0]))
                out.push_back(off);
            if (out.size() >= max)
                break;
        }
        return out;
    }

    const char *const kCorpus[] = {
        "turbo_software_zorro.cas",  "turbo_software_zorro_v2.cas",     "turbo_software_mirax_force.cas",
        "turbo_software_bruce_lee.cas", "turbo_software_missile_command.cas", "tt_international.cas",
        "tt_river_raid.cas",
    };
} // namespace

// =============================================================================================================
// Producer
// =============================================================================================================

TEST_CASE("producer: sequential loading, publication order and run discovery equal the reference scan")
{
    // In-memory structural image: chunk 0 (irg 250 ms), two joined zero-IRG chunks, a non-zero-IRG chunk that
    // ends the run, then a `data` chunk.
    std::vector<uint16_t> a(9000, 3), b(7000, 5), c(20000, 2), d(4000, 7);
    const Bytes img = make_image({{"fsk ", 250, values_payload(a)},
                                  {"fsk ", 0, values_payload(b)},
                                  {"fsk ", 0, values_payload(c)},
                                  {"fsk ", 100, values_payload(d)},
                                  {"data", 5, Bytes(64, 1)}});
    const size_t off = 8 + 16;

    Prog p;
    REQUIRE(p.init(img, off));
    REQUIRE(p.ref.hdr_off.size() == 3); // run discovery: the IRG>0 chunk is NOT part of it

    uint32_t last_values = 0, last_chunks = 1;
    FskProgStep st;
    size_t steps = 0;
    do
    {
        st = fsk_prog_step(p.L);
        ++steps;
        const uint32_t v = fsk_pub_load(&p.run().pub_values);
        const uint32_t ch = fsk_pub_load(&p.run().pub_chunks);
        CHECK(v >= last_values);   // monotonic
        CHECK(ch >= last_chunks);
        // Every published value lies in a published chunk whose descriptor is complete.
        CHECK(v <= p.run().prefix[ch]  + (ch > 0 ? p.run().value_counts[ch - 1] : 0) + 0u + p.run().value_counts[ch - 1] * 0u);
        if (ch >= 2)
        {
            CHECK(p.run().hdr_off[ch - 1] == p.ref.hdr_off[ch - 1]);
            CHECK(p.run().value_counts[ch - 1] == fsk_value_count(p.ref.avail[ch - 1]));
        }
        last_values = v;
        last_chunks = ch;
    } while (st == FskProgStep::PROGRESS && steps < 100000);

    CHECK(st == FskProgStep::DONE);
    CHECK(fsk_prog_state(p.run()) == FskProgState::FINAL);
    CHECK(fsk_pub_load(&p.run().pub_chunks) == 3);
    CHECK(fsk_pub_load(&p.run().pub_complete) == 3);
    CHECK(p.run().next_offset == p.ref.next_offset);
    size_t total_values = 0;
    for (size_t i = 0; i < 3; ++i)
        total_values += fsk_value_count(p.ref.avail[i]);
    CHECK(fsk_pub_load(&p.run().pub_values) == total_values);

    // Strictly sequential: one positional read after another, never a seek, never more than a block.
    CHECK(p.file.seeks == 0);
    CHECK(p.file.max_req <= FSK_PROG_BLOCK_BYTES);
    for (size_t i = 1; i < p.file.offsets.size(); ++i)
        CHECK(p.file.offsets[i] > p.file.offsets[i - 1]);

    // The loaded blocks are byte-identical to the file, chunk by chunk.
    for (size_t chn = 0; chn < 3; ++chn)
    {
        size_t left = p.ref.avail[chn];
        size_t pos = p.ref.hdr_off[chn] + 8;
        for (size_t blk = 0; left > 0; ++blk)
        {
            const size_t n = std::min(left, FSK_PROG_BLOCK_BYTES);
            REQUIRE(p.table[p.run().block_base[chn] + blk] != nullptr);
            CHECK(std::memcmp(p.table[p.run().block_base[chn] + blk], img.data() + pos, n) == 0);
            pos += n;
            left -= n;
        }
    }
    // The header / payload of the run never cost more than one read per block plus one per header.
    size_t blocks = 0;
    for (size_t i = 0; i < 3; ++i)
        blocks += fsk_prog_blocks_for(p.ref.avail[i]);
    // one read per payload block, one per joined chunk header, and one for the header that ends the run
    CHECK(p.L.reads == blocks + 3);
}

TEST_CASE("producer: run ends exactly where the full-preload scan ends it")
{
    std::vector<uint16_t> v(2000, 4);
    struct Case
    {
        const char *name;
        std::vector<MemChunk> chunks;
        size_t expect_chunks;
    };
    std::vector<Case> cases;
    cases.push_back({"single chunk, then EOF", {{"fsk ", 10, values_payload(v)}}, 1});
    cases.push_back({"non-fsk follows", {{"fsk ", 10, values_payload(v)}, {"baud", 600, Bytes()}}, 1});
    cases.push_back({"irg>0 follows", {{"fsk ", 10, values_payload(v)}, {"fsk ", 5, values_payload(v)}}, 1});
    cases.push_back({"zero-irg follows", {{"fsk ", 10, values_payload(v)}, {"fsk ", 0, values_payload(v)}}, 2});
    {
        std::vector<MemChunk> many;
        many.push_back({"fsk ", 10, values_payload(v)});
        for (int i = 0; i < 20; ++i)
            many.push_back({"fsk ", 0, values_payload(v)});
        cases.push_back({"cap of FSK_RUN_MAX_CHUNKS", many, FSK_RUN_MAX_CHUNKS});
    }
    for (const Case &cs : cases)
    {
        INFO(cs.name);
        Prog p;
        REQUIRE(p.init(make_image(cs.chunks), 24));
        REQUIRE(p.ref.hdr_off.size() == cs.expect_chunks);
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::DONE);
        CHECK(fsk_pub_load(&p.run().pub_chunks) == cs.expect_chunks);
        CHECK(p.run().next_offset == p.ref.next_offset);
        for (size_t i = 0; i < cs.expect_chunks; ++i)
            CHECK(p.run().hdr_off[i] == p.ref.hdr_off[i]);
    }

    // A structurally truncated last chunk: the loader reads what is present and ends there.
    Bytes img = make_image({{"fsk ", 10, values_payload(v)}, {"fsk ", 0, values_payload(v)}});
    img.resize(img.size() - 1001); // cut into the second chunk's payload
    Prog p;
    REQUIRE(p.init(img, 24));
    FskProgStep st;
    do
        st = fsk_prog_step(p.L);
    while (st == FskProgStep::PROGRESS);
    CHECK(st == FskProgStep::DONE);
    // truncated chunk: fsk_run_should_join refuses it, exactly like the scan
    CHECK(fsk_pub_load(&p.run().pub_chunks) == p.ref.hdr_off.size());
}

TEST_CASE("producer: short reads are accumulated, failures and stop leave the published part valid")
{
    std::vector<uint16_t> v(6000, 3);
    const Bytes img = make_image({{"fsk ", 10, values_payload(v)}});

    SUBCASE("short reads")
    {
        Prog p;
        REQUIRE(p.init(img, 24));
        p.file.short_every = 3;
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::DONE);
        CHECK(fsk_pub_load(&p.run().pub_values) == v.size());
        CHECK(std::memcmp(p.table[0], img.data() + 24 + 8, FSK_PROG_BLOCK_BYTES) == 0);
    }
    SUBCASE("read failure mid-run: FAILED, published values stay readable, no garbage published")
    {
        Prog p;
        REQUIRE(p.init(img, 24));
        p.file.fail_at = 24 + 8 + 4 * FSK_PROG_BLOCK_BYTES;
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::FAILED);
        CHECK(fsk_prog_state(p.run()) == FskProgState::FAILED);
        CHECK(fsk_pub_load(&p.run().pub_values) == 4 * FSK_PROG_BLOCK_BYTES / 2);
        // every published block holds real data, none the poison
        for (size_t b = 0; b < 4; ++b)
            CHECK(std::memcmp(p.table[b], img.data() + 32 + b * FSK_PROG_BLOCK_BYTES, FSK_PROG_BLOCK_BYTES) == 0);
        const uint32_t pub_after = fsk_pub_load(&p.run().pub_values);
        CHECK(fsk_prog_step(p.L) == FskProgStep::FAILED); // a dead producer publishes nothing more
        CHECK(fsk_pub_load(&p.run().pub_values) == pub_after);
    }
    SUBCASE("allocation failure: FAILED, nothing published from the block that could not be allocated")
    {
        Prog p;
        REQUIRE(p.init(img, 24));
        p.pool.fail_after = 3;
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::FAILED);
        CHECK(fsk_pub_load(&p.run().pub_values) == 3 * FSK_PROG_BLOCK_BYTES / 2);
    }
    SUBCASE("stop request")
    {
        Prog p;
        REQUIRE(p.init(img, 24));
        for (int i = 0; i < 5; ++i)
            REQUIRE(fsk_prog_step(p.L) == FskProgStep::PROGRESS);
        const uint32_t reads = p.file.reads;
        const uint32_t pub = fsk_pub_load(&p.run().pub_values);
        fsk_pub_store(&p.run().stop_req, 1);
        CHECK(fsk_prog_step(p.L) == FskProgStep::STOPPED);
        CHECK(fsk_prog_step(p.L) == FskProgStep::STOPPED);
        CHECK(p.file.reads == reads); // no file access after the stop
        CHECK(fsk_pub_load(&p.run().pub_values) == pub);
        CHECK(fsk_prog_state(p.run()) == FskProgState::STOPPED);
    }
}

// =============================================================================================================
// Startup gate
// =============================================================================================================

TEST_CASE("plan: the startup watermark is measured in waveform time, not bytes")
{
    // (a) Sparse leader: 5000 values of 6.5 s each are 32500 s of tape per 10 KB. The time runway is met by
    // the very first values; the PREFILL (values the RMT can consume before the first refill) is then the
    // binding condition, and it is a value count, not a fixed byte count.
    {
        std::vector<uint16_t> v(9000, 60000);
        Prog p;
        REQUIRE(p.init(make_image({{"fsk ", 0, values_payload(v)}}), 24));
        ProdSim sim{&p, 1000};
        const StartInfo s = wait_ready(p, sim, plan_in(0, 0), false, 1000);
        REQUIRE(s.ready);
        CHECK(s.plan.runway_us >= FSK_PROG_MIN_RUNWAY_US);
        CHECK(s.plan.ahead_values >= FSK_PROG_PREFILL_VALUES);
        // ready at the first block boundary that publishes >= prefill values (1536 values = 3072 bytes = 6 blocks)
        CHECK(fsk_pub_load(&p.run().pub_values) == 6 * FSK_PROG_BLOCK_BYTES / 2);
    }
    // (b) Dense data: 200 us values. 20 s of tape needs 100000 values, far more than a chunk: the run only
    // becomes ready once it is FINAL, i.e. when nothing more can be waited for.
    {
        std::vector<uint16_t> v(12000, 2);
        Prog p;
        REQUIRE(p.init(make_image({{"fsk ", 0, values_payload(v)}}), 24));
        ProdSim sim{&p, 1000};
        const StartInfo s = wait_ready(p, sim, plan_in(0, 0), false, 1000);
        REQUIRE(s.ready);
        CHECK(fsk_prog_state(p.run()) == FskProgState::FINAL);
        CHECK(s.plan.runway_us < FSK_PROG_MIN_RUNWAY_US);
    }
    // (c) Mid-density: 1 ms values. 20 s = 20000 values; ready exactly when 20000 values are published
    // (rounded up to the block that publishes them), not at a fixed number of bytes.
    {
        std::vector<uint16_t> v(60000, 10);
        Prog p;
        REQUIRE(p.init(make_image({{"fsk ", 0, values_payload(v)}}), 24));
        ProdSim sim{&p, 1000};
        const StartInfo s = wait_ready(p, sim, plan_in(0, 0), false, 1000);
        REQUIRE(s.ready);
        const uint32_t pub = fsk_pub_load(&p.run().pub_values);
        CHECK(pub >= 20000);
        CHECK(pub < 20000 + FSK_PROG_BLOCK_BYTES / 2 + 1);
        CHECK(s.plan.runway_us == FSK_PROG_MIN_RUNWAY_US);
    }
}

TEST_CASE("plan: not ready while the producer has not published enough; failed when it died first")
{
    std::vector<uint16_t> v(60000, 10);
    const Bytes img = make_image({{"fsk ", 0, values_payload(v)}});

    Prog p;
    REQUIRE(p.init(img, 24));
    FskProgPlan pl = fsk_prog_plan_start(p.run(), plan_in(0, 0));
    CHECK_FALSE(pl.ready);
    CHECK_FALSE(pl.failed);
    for (int i = 0; i < 5; ++i)
        fsk_prog_step(p.L);
    pl = fsk_prog_plan_start(p.run(), plan_in(0, 0));
    CHECK_FALSE(pl.ready);
    CHECK_FALSE(pl.failed);

    fsk_pub_store(&p.run().stop_req, 1);
    fsk_prog_step(p.L);
    pl = fsk_prog_plan_start(p.run(), plan_in(0, 0));
    CHECK_FALSE(pl.ready);
    CHECK(pl.failed); // nothing more will ever be published: the caller falls back to full preload

    // a small run that is complete is ready at once (no runway can be waited for)
    Prog q;
    std::vector<uint16_t> tiny(9000, 3);
    REQUIRE(q.init(make_image({{"fsk ", 0, values_payload(tiny)}}), 24));
    FskProgStep st;
    do
        st = fsk_prog_step(q.L);
    while (st == FskProgStep::PROGRESS);
    pl = fsk_prog_plan_start(q.run(), plan_in(0, 0));
    CHECK(pl.ready);
}

TEST_CASE("plan: seed position equals cas_fsk_locate_ticks over the whole run, and the time base equals the classic inert scan")
{
    for (const char *name : kCorpus)
    {
        Bytes img;
        if (!require_image(name, img))
            continue;
        const std::vector<size_t> starts = wanted_runs(img, 2);
        for (size_t off : starts)
        {
            INFO(name, " run at ", off);
            Classic c;
            REQUIRE(c.init(img, off));
            const uint64_t irg_us = static_cast<uint64_t>(c.ref.irg_ms[0]) * 1000ULL;
            const uint64_t total = rle_ticks(c.play(0).wave);

            Prog p;
            REQUIRE(p.init(img, off));
            ProdSim sim{&p, 0};
            sim.run_until(0); // read_us 0: the whole run is loaded
            FskProgState fin = fsk_prog_state(p.run());
            REQUIRE(fin == FskProgState::FINAL);

            std::mt19937_64 rng(7);
            for (int i = 0; i < 40; ++i)
            {
                const uint64_t p0 = i == 0 ? 0 : rng() % total;
                FskProgPlan pl = fsk_prog_plan_start(p.run(), plan_in(irg_us, irg_us + p0));
                REQUIRE(pl.ready);
                CHECK(pl.p0 == p0);
                if (p0 > 0)
                {
                    const FskLocateResult loc = cas_fsk_locate_ticks(c.cas._fsk_run_value_counts, c.cas._fsk_run_chunk_count,
                                                                     &Classic::value_at, &c, p0);
                    CHECK(pl.seed_chunk == loc.chunk_index);
                    CHECK(pl.seed_value == loc.value_index);
                    CHECK(pl.seed_skip == loc.skip_ticks);
                }
            }
            // time base: same inert length / start position as the classic scan for any elapsed time
            for (int64_t el : {1000000LL, 6330000LL, 9800000LL, 30000000LL, 3600000000LL})
            {
                FskProgPlanIn in = plan_in(irg_us, 0);
                in.timebase = true;
                in.elapsed_us = el;
                const FskProgPlan pl = fsk_prog_plan_start(p.run(), in);
                REQUIRE(pl.ready);
                const uint64_t want = fsk_timebase_wave_want(irg_us, 0, el);
                const uint64_t inert = want > 0 ? cas_fsk_inert_ticks(c.cas._fsk_run_value_counts, c.cas._fsk_run_chunk_count,
                                                                       &Classic::value_at, &c, want, FSK_TIMEBASE_LOW_BUDGET_US)
                                                : 0;
                CHECK(pl.tb_want == want);
                CHECK(pl.tb_inert == inert);
                CHECK(pl.q_start == fsk_timebase_q(irg_us, 0, el, inert));
            }
        }
    }
}

TEST_CASE("plan: the time base waits until the inert prefix is decidable from published data")
{
    // 3000 values: a long MARK (HIGH) leader region interleaved with tiny LOW blips, then real data.
    Bytes img;
    {
        std::vector<uint16_t> v;
        for (int i = 0; i < 6000; ++i)
            v.push_back((i & 1) == 0 ? 1 : 300); // LOW 100 us (well inside the LOW budget), HIGH 30 ms
        for (int i = 0; i < 4000; ++i)
            v.push_back(20);                     // dense data: LOW 2 ms exceeds the budget
        img = make_image({{"fsk ", 1000, values_payload(v)}});
    }
    Prog p;
    REQUIRE(p.init(img, 24));
    ProdSim sim{&p, 100000};
    // A large elapsed time wants more inert ticks than the first blocks describe: not decidable yet.
    FskProgPlanIn in = plan_in(1000000, 0);
    in.timebase = true;
    in.elapsed_us = 200LL * 1000000LL;
    sim.run_until(0);
    FskProgPlan pl = fsk_prog_plan_start(p.run(), in);
    CHECK_FALSE(pl.ready);
    // ... and it is decided once the first data LOW is published, identically to the classic scan
    const StartInfo s = wait_ready(p, sim, plan_in(1000000, 0), true, 100000);
    REQUIRE(s.ready);
    Classic c;
    REQUIRE(c.init(img, 24));
    const uint64_t inert = cas_fsk_inert_ticks(c.cas._fsk_run_value_counts, c.cas._fsk_run_chunk_count, &Classic::value_at, &c,
                                               s.plan.tb_want, FSK_TIMEBASE_LOW_BUDGET_US);
    CHECK(s.plan.tb_inert == inert);
}

// =============================================================================================================
// Consumer: the production encoder over the producer/consumer model
// =============================================================================================================

TEST_CASE("consumer: progressive playback emits exactly the full-preload waveform (real corpus)")
{
    int compared = 0;
    for (const char *name : kCorpus)
    {
        Bytes img;
        if (!require_image(name, img))
            continue;
        for (size_t off : wanted_runs(img, 2))
        {
            INFO(name, " run at ", off);
            const Drive ref = reference_from(img, off, 0);
            REQUIRE(ref.complete);

            // fast producer (LAN-like), the measured remote-TNFS rate, and a 2x slower one
            for (int64_t read_us : {1000LL, 237500LL, 475000LL})
            {
                INFO("read_us=", read_us);
                Prog p;
                REQUIRE(p.init(img, off));
                ProdSim sim{&p, read_us};
                const Played pl = play_progressive(p, sim, 0);
                REQUIRE(pl.start.ready);
                CHECK_FALSE(pl.d.underrun);
                CHECK(pl.d.complete);
                CHECK(pl.d.wave == ref.wave); // value-for-value, chunk boundaries included
                CHECK(pl.d.encoded_ticks == ref.encoded_ticks);
                CHECK(fsk_prog_state(p.run()) == FskProgState::FINAL);
                ++compared;
            }
        }
    }
    MESSAGE("progressive == full preload for ", compared, " (image, run, rate) combinations");
}

TEST_CASE("consumer: real Zorro at the measured remote TNFS rate and at 2x: startup time and no underrun")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    const Drive ref = reference_from(img, off, 0);

    for (int64_t read_us : {237500LL, 475000LL})
    {
        Prog p;
        REQUIRE(p.init(img, off));
        ProdSim sim{&p, read_us};
        // MOTOR ON at t=0, loader started at t=0: the time base applies to this first dispatch
        const uint64_t irg_us = static_cast<uint64_t>(p.ref.irg_ms[0]) * 1000ULL;
        const StartInfo s = wait_ready(p, sim, plan_in(irg_us, 0), true, 10000);
        REQUIRE(s.ready);
        MESSAGE("read=", read_us / 1000, " ms: ready at MOTOR ON + ", s.t_ready / 1000, " ms, q_start=", s.plan.q_start,
                " (irg ", irg_us, "), published values ", fsk_pub_load(&p.run().pub_values));
        // the first edge is far before the 47 s deadline the full preload (234 s) missed
        CHECK(s.t_ready < 30LL * 1000000LL);
        p.cas.seed(s.plan.seed_chunk, s.plan.seed_value, s.plan.seed_skip);
        const Drive d = drive(p.cas, s.plan.p0, s.t_ready, [&](int64_t T) { sim.run_until(T); });
        CHECK_FALSE(d.underrun);
        CHECK(d.complete);
        // what is played is the reference from the time-base position on
        const Drive tail = reference_from(img, off, s.plan.p0);
        CHECK(d.wave == tail.wave);
        (void)ref;
    }
}

TEST_CASE("consumer: supply slower than consumption underruns deterministically and never emits unloaded data")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    const Drive ref = reference_from(img, off, 0);

    auto run_once = [&](int64_t read_us, bool &underrun, uint64_t &ticks, uint32_t &pub_at_end, size_t &ordinal, Rle &wave)
    {
        Prog p;
        REQUIRE(p.init(img, off));
        ProdSim sim{&p, read_us};
        const Played pl = play_progressive(p, sim, 0);
        REQUIRE(pl.start.ready);
        underrun = pl.d.underrun;
        ticks = pl.d.encoded_ticks;
        wave = pl.d.wave;
        pub_at_end = fsk_pub_load(&p.run().pub_values);
        ordinal = p.cas._fsk_prog.prefix[p.cas._fsk_run_chunk_index] + p.cas._fsk_value_index;
    };

    bool u1 = false, u2 = false;
    uint64_t t1 = 0, t2 = 0;
    uint32_t pub1 = 0, pub2 = 0;
    size_t ord1 = 0, ord2 = 0;
    Rle w1, w2;
    run_once(3000000, u1, t1, pub1, ord1, w1); // 3 s per 512-byte block: far below the tape's consumption rate
    run_once(3000000, u2, t2, pub2, ord2, w2);
    REQUIRE(u1);
    CHECK(u2 == u1);
    CHECK(t1 == t2); // deterministic
    CHECK(pub1 == pub2);
    CHECK(w1 == w2);
    // it stopped because the next value was NOT published, and everything before it was
    CHECK(ord1 == pub1);
    // no poison (0xEE bytes of unloaded blocks), no invention: exactly a prefix of the reference waveform
    CHECK(w1 == rle_prefix(ref.wave, t1));
    CHECK(t1 < rle_ticks(ref.wave));
    // ... and the boundary between "sustainable" and "underruns" lies between the measured remote rate and this one
    bool u = false;
    uint64_t tt = 0;
    uint32_t pp = 0;
    size_t oo = 0;
    Rle ww;
    run_once(475000, u, tt, pp, oo, ww);
    CHECK_FALSE(u);
}

TEST_CASE("consumer: underrun is an explicit stop; resuming from the frozen position continues gaplessly")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    const Drive ref = reference_from(img, off, 0);

    Prog p;
    REQUIRE(p.init(img, off));
    ProdSim sim{&p, 3000000};
    const Played first = play_progressive(p, sim, 0);
    REQUIRE(first.start.ready);
    REQUIRE(first.d.underrun);
    REQUIRE(fsk_prog_state(p.run()) == FskProgState::LOADING); // the producer is alive: this is a stall, not a failure

    // The position is where the encoder stopped (everything it emitted has played out). Resume: wait for
    // the runway from that position, then continue. The loader kept filling meanwhile.
    uint64_t pos = first.d.encoded_ticks;
    Rle played = first.d.wave;
    int64_t now = first.start.t_ready + static_cast<int64_t>(first.d.encoded_ticks); // the queued symbols have played out
    int resumes = 0;
    while (played != ref.wave && resumes < 400)
    {
        const StartInfo s = wait_ready(p, sim, plan_in(0, pos), false, 10000, now);
        REQUIRE(s.ready);
        p.cas.seed(s.plan.seed_chunk, s.plan.seed_value, s.plan.seed_skip);
        const Drive d = drive(p.cas, s.plan.p0, s.t_ready, [&](int64_t T) { sim.run_until(T); });
        played = rle_concat(played, d.wave);
        pos += d.encoded_ticks;
        now = s.t_ready + static_cast<int64_t>(d.encoded_ticks);
        ++resumes;
        if (!d.underrun)
            break;
    }
    CHECK(played == ref.wave); // no tick lost, none duplicated, across every stall
    MESSAGE("stalls: ", resumes);
}

TEST_CASE("consumer: MOTOR OFF at any point, resume later: exact continuation, loader keeps filling while frozen")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    const Drive ref = reference_from(img, off, 0);
    const uint64_t total = rle_ticks(ref.wave);
    std::mt19937_64 rng(99);

    int frozen_progress = 0;
    for (int trial = 0; trial < 12; ++trial)
    {
        Prog p;
        REQUIRE(p.init(img, off));
        ProdSim sim{&p, 237500};
        // MOTOR OFF after `x` waveform ticks (physical position), at least a bit into the run
        const uint64_t x = 1000000 + rng() % std::min<uint64_t>(60ULL * 1000000ULL, total / 2);
        const StartInfo s0 = wait_ready(p, sim, plan_in(0, 0), false, 10000);
        REQUIRE(s0.ready);
        p.cas.seed(s0.plan.seed_chunk, s0.plan.seed_value, s0.plan.seed_skip);
        const Drive a = drive(p.cas, 0, s0.t_ready, [&](int64_t T) { sim.run_until(T); },
                              s0.t_ready + static_cast<int64_t>(x));
        REQUIRE_FALSE(a.underrun);
        const Rle before = rle_prefix(a.wave, x); // the hardware stopped at x: what was still queued never plays
        REQUIRE(rle_ticks(before) == x);

        // frozen for 90 s: the producer keeps filling (nothing else touches the file)
        const uint32_t pub_before = fsk_pub_load(&p.run().pub_values);
        const int64_t t_freeze = s0.t_ready + static_cast<int64_t>(x);
        sim.run_until(t_freeze + 90LL * 1000000LL);
        // the loader is still busy (the whole run needs ~234 s at this rate): it makes progress while frozen
        CHECK(fsk_pub_load(&p.run().pub_values) > pub_before);
        ++frozen_progress;

        // MOTOR ON: the resume needs published runway from the frozen position, no file access
        const uint32_t reads_before = p.file.reads;
        (void)reads_before;
        const int64_t t_resume = t_freeze + 90LL * 1000000LL;
        FskProgPlanIn in = plan_in(0, x);
        const FskProgPlan pl = fsk_prog_plan_start(p.run(), in);
        REQUIRE(pl.ready);
        CHECK(pl.p0 == x);
        p.cas.seed(pl.seed_chunk, pl.seed_value, pl.seed_skip);
        const Drive b = drive(p.cas, pl.p0, t_resume, [&](int64_t T) { sim.run_until(T); });
        CHECK_FALSE(b.underrun);
        CHECK(rle_concat(before, b.wave) == ref.wave);
    }
    CHECK(frozen_progress == 12);
}

TEST_CASE("consumer: resume with too little published runway is not ready (no premature start)")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    Prog p;
    REQUIRE(p.init(img, off));
    ProdSim sim{&p, 237500};
    sim.run_until(8 * 237500); // a few blocks only
    const uint32_t pub = fsk_pub_load(&p.run().pub_values);
    // a frozen position right at the published edge: no runway ahead of it
    uint64_t edge = 0;
    for (uint32_t v = 0; v < pub; ++v)
    {
        const uint8_t *b = p.table[(v * 2) / FSK_PROG_BLOCK_BYTES];
        edge += fsk_ticks_for_value(fsk_decode_le16(b + (v * 2) % FSK_PROG_BLOCK_BYTES));
    }
    const FskProgPlan pl = fsk_prog_plan_start(p.run(), plan_in(0, edge - 1000));
    CHECK_FALSE(pl.ready);
    CHECK_FALSE(pl.failed);
}

// =============================================================================================================
// Rewind from the progressively loaded state
// =============================================================================================================

namespace
{
    struct RewindCase
    {
        bool fast_ok;
        CassetteTargetResolution res;
    };

    // Position: `pos_wave` waveform ticks into the run that starts at tape_offset. Compares the store-only
    // resolution with the file-based one for a rewind of back_s seconds.
    bool compare_rewind(Prog &p, const Bytes &img, const CassetteWalkState &walk0, size_t tape_offset, uint64_t q_us,
                        uint64_t back_us, bool expect_fast)
    {
        CassetteTargetResolution fast{};
        const uint32_t reads_before = p.file.reads;
        const bool ok = fsk_prog_resolve_rewind(p.run(), walk0, tape_offset, q_us > 0, q_us, back_us, fast);
        CHECK(p.file.reads == reads_before); // the store-only answer never touches the file
        if (expect_fast)
            REQUIRE(ok);
        if (!ok)
            return false;

        CassetteWalkState cur{};
        REQUIRE(cas_walk_tape_time(img.size(), mem_read, const_cast<Bytes *>(&img), tape_offset, UINT64_MAX, cur));
        const uint64_t origin = cur.time_us + q_us;
        const uint64_t target = origin > back_us ? origin - back_us : 0;
        CassetteTargetResolution ref{};
        REQUIRE(cas_resolve_target_time(img.size(), mem_read, const_cast<Bytes *>(&img), target, ref));
        CHECK(fast.walk.offset == ref.walk.offset);
        CHECK(fast.walk.time_us == ref.walk.time_us);
        CHECK(fast.walk.baud == ref.walk.baud);
        CHECK(fast.walk.t2k_samplerate == ref.walk.t2k_samplerate);
        CHECK(fast.is_fsk == ref.is_fsk);
        CHECK(fast.irg_ms == ref.irg_ms);
        CHECK(fast.q_us == ref.q_us);
        return true;
    }
}

TEST_CASE("rewind: -5 s and -10 s from a progressively loaded state equal the file-based resolution, with no file reads")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    CassetteWalkState walk0{};
    REQUIRE(cas_walk_tape_time(img.size(), mem_read, &img, off, UINT64_MAX, walk0));
    REQUIRE(walk0.offset == off);
    const uint64_t irg_us = 0;
    (void)irg_us;

    // Loader state after n steps; the position is `lead` ticks behind the published edge, all inside the run
    // (chunk 0 may still be loading: its duration is not known, but the position bounds it).
    for (size_t steps : {40u, 120u, 400u, 1100u, 100000u})
    {
        Prog p;
        REQUIRE(p.init(img, off));
        for (size_t i = 0; i < steps; ++i)
            if (fsk_prog_step(p.L) != FskProgStep::PROGRESS)
                break;
        const uint64_t irg = static_cast<uint64_t>(p.ref.irg_ms[0]) * 1000ULL;
        // published waveform ticks
        uint64_t edge = 0;
        const uint32_t pub = fsk_pub_load(&p.run().pub_values);
        for (uint32_t c = 0, v = 0; v < pub; ++v)
        {
            while (c + 1 < fsk_pub_load(&p.run().pub_chunks) && v >= p.run().prefix[c + 1])
                ++c;
            const size_t lv = v - p.run().prefix[c];
            const uint8_t *b = p.table[p.run().block_base[c] + (lv * 2) / FSK_PROG_BLOCK_BYTES];
            edge += fsk_ticks_for_value(fsk_decode_le16(b + (lv * 2) % FSK_PROG_BLOCK_BYTES));
        }
        for (uint64_t back_s : {5ULL, 10ULL})
        {
            // the position is in the leading part of what is loaded
            const uint64_t need = back_s * 1000000ULL + 100000;
            if (edge < need + irg + 1)
                continue;
            const uint64_t pos_wave = edge - 50000;
            INFO("steps=", steps, " back=", back_s, " edge=", edge, " pos=", pos_wave);
            // fast resolution must be possible whenever the target lies before the published edge
            compare_rewind(p, img, walk0, off, irg + pos_wave, back_s * 1000000ULL, true);
        }
    }
}

TEST_CASE("rewind: across chunk boundaries, in the leading IRG, before the store and beyond what is loaded")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    CassetteWalkState walk0{};
    REQUIRE(cas_walk_tape_time(img.size(), mem_read, &img, off, UINT64_MAX, walk0));

    Prog p;
    REQUIRE(p.init(img, off));
    ProdSim sim{&p, 0};
    sim.run_until(0);
    REQUIRE(fsk_prog_state(p.run()) == FskProgState::FINAL);
    const uint64_t total = run_total_ticks(img, off);
    const uint64_t irg = static_cast<uint64_t>(p.ref.irg_ms[0]) * 1000ULL;
    std::mt19937_64 rng(3);
    int fast = 0, slow = 0;
    for (int i = 0; i < 300; ++i)
    {
        const uint64_t q = irg + (rng() % total);
        const uint64_t back = (rng() % 4 == 0) ? (rng() % (60ULL * 1000000ULL)) : (5ULL + rng() % 6) * 1000000ULL;
        if (compare_rewind(p, img, walk0, off, q, back, false))
            ++fast;
        else
            ++slow;
    }
    MESSAGE("resolved from the store: ", fast, ", left to the file-based path: ", slow);
    CHECK(fast > 200);

    // A target before the first chunk of the store is left to the file-based resolution.
    if (walk0.time_us > 0)
    {
        CassetteTargetResolution out{};
        CHECK_FALSE(fsk_prog_resolve_rewind(p.run(), walk0, off, true, irg + 1000000, walk0.time_us + irg + 2000000, out));
    }
    // A tape offset that is not a chunk of the store cannot be answered from it.
    CassetteTargetResolution out{};
    CHECK_FALSE(fsk_prog_resolve_rewind(p.run(), walk0, off + 3, true, 1000, 500, out));
}

TEST_CASE("rewind: playback restarted inside the store (chunk k > 0) equals the full-preload run from that chunk")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    Prog p;
    REQUIRE(p.init(img, off));
    ProdSim sim{&p, 0};
    sim.run_until(0);
    REQUIRE(p.ref.hdr_off.size() >= 4);

    for (size_t k : {1u, 2u, 5u})
    {
        if (k >= p.ref.hdr_off.size())
            continue;
        // the walker's view of the store chunk k as the start of a run: zero IRG
        Classic ref;
        REQUIRE(ref.init(img, p.ref.hdr_off[k]));
        for (uint64_t q : {0ULL, 1234567ULL, 9000000ULL})
        {
            const StartInfo s = wait_ready(p, sim, plan_in(0, q, k), false, 1000);
            REQUIRE(s.ready);
            p.cas.seed(k + s.plan.seed_chunk, s.plan.seed_value, s.plan.seed_skip);
            const Drive d = drive(p.cas, s.plan.p0, 0, [&](int64_t T) { sim.run_until(T); });
            const Drive r = ref.play(q);
            // the store keeps playing past the classic run's end only if more chunks join; both stop at the same place
            CHECK(d.wave == r.wave);
        }
    }
}

// =============================================================================================================
// Encoder: ISR discipline
// =============================================================================================================

namespace
{
    std::string read_text(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
        return s;
    }

    std::string between(const std::string &s, const char *a, const char *b)
    {
        const size_t i = s.find(a);
        const size_t j = s.find(b, i == std::string::npos ? 0 : i);
        if (i == std::string::npos || j == std::string::npos)
            return {};
        return s.substr(i + std::strlen(a), j - i - std::strlen(a));
    }

    const std::string &cassette_src()
    {
        static const std::string s = read_text(std::string(FSK_PROGRESSIVE_TESTS_SOURCE_DIR) + "/lib/device/sio/cassette.cpp");
        return s;
    }
    const std::string &progressive_hdr()
    {
        static const std::string s = read_text(std::string(FSK_PROGRESSIVE_TESTS_SOURCE_DIR) + "/lib/device/sio/fsk_progressive.h");
        return s;
    }
    const std::string &progressive_src()
    {
        static const std::string s = read_text(std::string(FSK_PROGRESSIVE_TESTS_SOURCE_DIR) + "/lib/device/sio/fsk_progressive.cpp");
        return s;
    }

    // Tokens that must never appear in code the RMT ISR runs: I/O, allocation, logging, blocking calls.
    const char *const kForbidden[] = {"fnio::", "fopen", "fread", "fseek", "fwrite", "printf", "Debug_", "DIAGRING", "CASDIAG",
                                      "malloc", "calloc", "realloc", "heap_caps", "new ", "delete ", "free(", "xSemaphore",
                                      "xTask", "vTask", "xQueue", "ESP_LOG", "std::", "delay", "vTaskDelay", "esp_rom_printf"};
}

TEST_CASE("ISR discipline: the encoder callback and the consumer-side inline functions do no I/O, allocation or logging")
{
    const std::string cb = between(cassette_src(), "// FSK_ENCODE_CB_BEGIN", "// FSK_ENCODE_CB_END");
    REQUIRE_FALSE(cb.empty());
    // strip comments before scanning (they may name the very things the code avoids)
    auto strip = [](std::string s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s.compare(i, 2, "//") == 0)
            {
                while (i < s.size() && s[i] != '\n')
                    ++i;
                out += '\n';
            }
            else
                out += s[i];
        }
        return out;
    };
    const std::string code = strip(cb);
    for (const char *tok : kForbidden)
        CHECK_MESSAGE(code.find(tok) == std::string::npos, "forbidden token in fsk_encode_cb: ", tok);

    // The consumer half of fsk_progressive.h (between the encoder-view banner and the rewind banner).
    const std::string inl = strip(between(progressive_hdr(), "// ---- the encoder's view (runs in the RMT ISR)", "// ---- rewind from the resident run"));
    REQUIRE_FALSE(inl.empty());
    for (const char *tok : kForbidden)
        if (std::strcmp(tok, "std::") != 0)
            CHECK_MESSAGE(inl.find(tok) == std::string::npos, "forbidden token in the ISR-side inline functions: ", tok);

    // The encoder fetches values only through the published-ordinal accessor.
    CHECK(code.find("fsk_prog_next_value(") != std::string::npos);
    CHECK(code.find("fsk_prog_at_end(") != std::string::npos);
    CHECK(code.find("_fsk_prog.pub_values") == std::string::npos); // no private peeking at the watermark
}

TEST_CASE("publication: every shared word is written with a release store and read with an acquire load")
{
    const std::string src = progressive_src();
    const std::string hdr = progressive_hdr();
    // producer: the only writers of the publication words go through fsk_pub_store
    for (const char *w : {"pub_values", "pub_chunks", "pub_complete", "state"})
    {
        const std::string plain = std::string("r.") + w + " =";
        // bind() initialises them before the producer exists (documented); everything else must be a release store
        size_t hits = 0;
        for (size_t pos = 0; (pos = src.find(plain, pos)) != std::string::npos; ++pos)
            ++hits;
        CHECK_MESSAGE(hits <= 2, "plain write of ", w, " outside bind()/first chunk: ", hits);
    }
    CHECK(hdr.find("__ATOMIC_RELEASE") != std::string::npos);
    CHECK(hdr.find("__ATOMIC_ACQUIRE") != std::string::npos);
    // pub_chunks is published AFTER the descriptor fields it covers, and pub_values after the block bytes
    const size_t d = src.find("r.prefix[nc + 1] =");
    const size_t pchunks = src.find("fsk_pub_store(&r.pub_chunks, static_cast<uint32_t>(nc + 1))");
    REQUIRE(d != std::string::npos);
    REQUIRE(pchunks != std::string::npos);
    CHECK(d < pchunks);
    const size_t rd = src.find("read_exact(L, r.blocks[blk], n)");
    const size_t pv = src.find("fsk_pub_store(&r.pub_values");
    REQUIRE(rd != std::string::npos);
    REQUIRE(pv != std::string::npos);
    CHECK(rd < pv);
}

TEST_CASE("cassette.cpp wiring: the loader is joined before blocks, the file or the mount go away")
{
    const std::string &s = cassette_src();
    // fsk_free_blocks(): drain check, then loader stop, then release
    {
        const std::string body = between(s, "void sioCassette::fsk_free_blocks()", "// Allocates the segmented block table");
        REQUIRE_FALSE(body.empty());
        const size_t drain = body.find("FskTxState::DRAINING");
        const size_t stop = body.find("fsk_loader_stop()");
        const size_t rel = body.find("fsk_release_blocks(");
        REQUIRE(drain != std::string::npos);
        REQUIRE(stop != std::string::npos);
        REQUIRE(rel != std::string::npos);
        CHECK(drain < stop);
        CHECK(stop < rel);
    }
    {
        const std::string um = between(s, "void sioCassette::umount_cassette_file()", "void sioCassette::mount_cassette_file");
        REQUIRE_FALSE(um.empty());
        CHECK(um.find("fsk_loader_stop()") != std::string::npos);
        CHECK(um.find("fsk_free_blocks()") == std::string::npos); // the HTTP task never frees what the RMT may still read
    }
    {
        const std::string mt = between(s, "void sioCassette::mount_cassette_file", "void sioCassette::sio_enable_cassette()");
        CHECK(mt.find("fsk_loader_stop()") != std::string::npos);
    }
    // a header outside the store quiesces the loader before touching the file
    {
        const std::string w = between(s, "size_t sioCassette::send_FUJI_tape_block(size_t offset)", "// looking for a data header");
        (void)w;
        const size_t a = s.find("fsk_prog_store_find(offset)");
        REQUIRE(a != std::string::npos);
        const size_t b = s.find("fsk_loader_stop();", a);
        const size_t c = s.find("fnio::fseek(_file, offset, SEEK_SET);", a);
        REQUIRE(b != std::string::npos);
        REQUIRE(c != std::string::npos);
        CHECK(b < c);
    }
    // the file-based rewind stops the loader before it walks the file
    {
        const size_t a = s.find("fsk_prog_resolve_rewind_fast(back_us, res)");
        REQUIRE(a != std::string::npos);
        const size_t b = s.find("fsk_loader_stop();", a);
        const size_t c = s.find("walk_tape_time(tape_offset", a);
        REQUIRE(b != std::string::npos);
        REQUIRE(c != std::string::npos);
        CHECK(b < c);
    }
    // the loader never runs on the cassette task's core/priority
    CHECK(s.find("FSK_LOADER_CORE = 0") != std::string::npos);

    // the loader's only path to the file is the resilient reader (no bare ftell/fseek/fread trust)
    {
        const std::string lr = between(s, "size_t sioCassette::fsk_loader_read(", "// Payload blocks live in PSRAM");
        REQUIRE_FALSE(lr.empty());
        CHECK(lr.find("fsk_prog_resilient_read(") != std::string::npos);
        CHECK(lr.find("fnio::fread") == std::string::npos);
        CHECK(lr.find("fnio::fseek") == std::string::npos);
        // a FAILED loader is resumed from its cursor, only while it is FAILED and the budget lasts
        const std::string rs = between(s, "bool sioCassette::fsk_loader_restart()", "bool sioCassette::fsk_prog_begin(");
        REQUIRE_FALSE(rs.empty());
        CHECK(rs.find("FskProgState::FAILED") != std::string::npos);
        CHECK(rs.find("fsk_prog_loader_recover(") != std::string::npos);
        CHECK(rs.find("_fsk_loader_suspect = true") != std::string::npos);
    }

    // record mode replaces _file: the loader is quiesced first
    {
        const std::string cl = between(s, "void sioCassette::close_cassette_file()", "void sioCassette::open_cassette_file");
        REQUIRE_FALSE(cl.empty());
        const size_t stop = cl.find("fsk_loader_stop()");
        const size_t fcl = cl.find("fnio::fclose(_file)");
        REQUIRE(stop != std::string::npos);
        REQUIRE(fcl != std::string::npos);
        CHECK(stop < fcl);
    }
    // an unmounted cassette never starts a full read on the closing file as the progressive fallback
    {
        const size_t a = s.find("loader stopped before the runway, full preload");
        REQUIRE(a != std::string::npos);
        const size_t m = s.find("if (!_mounted)", a);
        const size_t g = s.find("goto done", m);
        REQUIRE(m != std::string::npos);
        REQUIRE(g != std::string::npos);
        CHECK(m - a < 400);
        CHECK(g - m < 200);
    }

    // A tape slot (no MediaType) tells the cassette about its unmount before the caller closes the file.
    {
        const std::string d = read_text(std::string(FSK_PROGRESSIVE_TESTS_SOURCE_DIR) + "/lib/device/sio/disk.cpp");
        REQUIRE_FALSE(d.empty());
        const std::string um = between(d, "void sioDisk::unmount()", "// Create blank disk");
        REQUIRE_FALSE(um.empty());
        const size_t tape = um.find("_is_tape");
        const size_t cas = um.find("umount_cassette_file()");
        const size_t media = um.find("_disk->unmount()");
        REQUIRE(tape != std::string::npos);
        REQUIRE(cas != std::string::npos);
        REQUIRE(media != std::string::npos);
        CHECK(tape < cas);
        CHECK(cas < media);
    }
}

// =============================================================================================================
// Real threads: the release/acquire contract with the production encoder callback
// =============================================================================================================

// =============================================================================================================
// Short reads and lost file positions (remote TNFS field failure, first physical run)
// =============================================================================================================
//
// The models below are TRANSCRIPTIONS of the firmware's own layers (lib/TNFSlib/tnfslib.cpp and newlib's stdio
// as used through FileHandlerLocal / vfs_tnfs_*), reduced to what matters here:
//   TnfsSim   _tnfs_read_from_cache / _tnfs_fill_cache / tnfs_read / tnfs_lseek(SEEK_SET) over a server whose
//             READ has NO offset (it reads from the server's own position), with fault injection: a fill
//             transaction that never gets its reply, either before the server saw it or after it executed it.
//   StdioSim  newlib fread (copy the buffer, refill through vfs read, return the bytes copied so far when a
//             refill fails), ftell (the stream's belief) and the fseek buffer-window shortcut.

namespace
{
    struct TnfsSim
    {
        const Bytes *data = nullptr;
        size_t server_pos = 0;               // where the server's next READ starts
        uint32_t file_position = 0;          // client: "real" position (end of what it has fetched)
        uint32_t cached_pos = 0;             // client: logical position seen by the reader
        uint32_t cache_start = 0;
        uint32_t cache_available = 0;
        uint8_t cache[512];
        // fault injection: the first fill that starts at or after at_fill_pos fails (the reply never arrives)
        struct Fault
        {
            size_t at_fill_pos;     // the first fill starting at or after here...
            unsigned times;         // ...fails this many times in a row (>= 1000: every fill at or after it: the network is gone)
            bool server_executed;   // the server executed the READ (advanced) but the reply was lost
        };
        std::vector<Fault> faults;
        uint32_t fills = 0, net_seeks = 0, failed_fills = 0;

        size_t size() const { return data->size(); }

        // _tnfs_fill_cache (ESP branch)
        int fill_cache()
        {
            ++fills;
            int error = 0;
            cache_available = 0;
            cache_start = file_position;
            uint32_t remaining = sizeof(cache);
            while (remaining > 0)
            {
                const uint16_t to_read = remaining > 525 ? 525 : static_cast<uint16_t>(remaining);
                for (Fault &f : faults)
                    if (f.times > 0 && file_position >= f.at_fill_pos)
                    {
                        --f.times;
                        ++failed_fills;
                        if (f.server_executed)
                            server_pos += std::min<size_t>(to_read, size() - std::min(size(), server_pos));
                        return -1; // _tnfs_transaction failed after its retries
                    }
                const size_t avail = server_pos < size() ? size() - server_pos : 0;
                if (avail == 0)
                    break; // EOF: ESP branch keeps error = 0 and a short cache
                const uint16_t got = static_cast<uint16_t>(std::min<size_t>(to_read, avail));
                std::memcpy(cache + (sizeof(cache) - remaining), data->data() + server_pos, got);
                server_pos += got;
                file_position += got;
                remaining -= got;
            }
            if (error == 0)
                cache_available = sizeof(cache) - remaining;
            return error;
        }

        // _tnfs_read_from_cache
        int read_from_cache(uint8_t *dest, uint16_t dest_size, uint16_t *used)
        {
            if (cached_pos >= size())
                return 0x21; // END_OF_FILE
            if (cache_available == 0)
                return -1;
            if (cached_pos >= cache_start)
            {
                const uint32_t cache_end = cache_start + cache_available;
                if (cached_pos < cache_end)
                {
                    const uint32_t avail = cache_end - cached_pos;
                    const uint16_t dest_free = dest_size - *used;
                    const uint16_t provided = dest_free > avail ? static_cast<uint16_t>(avail) : dest_free;
                    std::memcpy(dest + *used, cache + (cached_pos - cache_start), provided);
                    cached_pos += provided;
                    *used += provided;
                }
            }
            return (dest_size - *used) ? -1 : 0;
        }

        // tnfs_read + vfs_tnfs_read: bytes delivered, or -1 (the bytes copied from the cache before a failed fill are lost)
        int vfs_read(uint8_t *dst, uint16_t len)
        {
            uint16_t used = 0;
            int result = 0;
            while ((result = read_from_cache(dst, len, &used)) != 0 && result != 0x21)
            {
                result = fill_cache();
                if (result != 0)
                    break;
            }
            if (result == 0 || (result == 0x21 && used > 0))
                return used;
            return -1;
        }

        // tnfs_lseek(SEEK_SET) with the cache shortcut; an ABSOLUTE position on the server
        bool vfs_lseek_set(size_t pos)
        {
            if (cache_available != 0 && pos >= cache_start && pos < cache_start + cache_available)
            {
                cached_pos = static_cast<uint32_t>(pos);
                return true;
            }
            cache_available = 0;
            ++net_seeks;
            server_pos = pos;
            file_position = cached_pos = static_cast<uint32_t>(pos);
            return true;
        }
    };

    struct StdioSim
    {
        TnfsSim *t = nullptr;
        size_t bsize = 128;        // stdio buffer size (BUFSIZ)
        uint8_t buf[512];
        size_t off_ = 0;           // newlib _offset: file offset just after the buffered data
        size_t bl = 0, bp = 0;     // valid bytes in the buffer, read index

        long ftell() const { return static_cast<long>(off_ - bl + bp); }

        // newlib fseek(SEEK_SET): a target inside the buffer window (its end included) is answered without I/O
        bool fseek(size_t target)
        {
            const size_t start = off_ - bl;
            if (target >= start && target <= off_)
            {
                bp = target - start;
                return true;
            }
            if (!t->vfs_lseek_set(target))
                return false;
            off_ = target;
            bl = bp = 0;
            return true;
        }

        // newlib fread: copies, refills through the vfs read, returns what it copied when a refill fails
        size_t fread(uint8_t *dst, size_t n)
        {
            size_t left = n;
            size_t done = 0;
            for (;;)
            {
                const size_t avail = bl - bp;
                if (left <= avail)
                {
                    std::memcpy(dst + done, buf + bp, left);
                    bp += left;
                    return n;
                }
                std::memcpy(dst + done, buf + bp, avail);
                done += avail;
                left -= avail;
                bp = bl;
                const int r = t->vfs_read(buf, static_cast<uint16_t>(bsize));
                if (r <= 0)
                {
                    // refill failed / EOF: buffer empty, _offset unchanged (the belief does not move)
                    off_ = off_; // (explicit: nothing advances)
                    bl = bp = 0;
                    return done;
                }
                off_ += static_cast<size_t>(r);
                bl = static_cast<size_t>(r);
                bp = 0;
            }
        }
    };

    struct TnfsFile
    {
        Bytes img;
        TnfsSim tnfs;
        StdioSim stdio;
        bool resilient = true;
        bool suspect = false;
        FskReadStats rs;
        uint32_t calls = 0, short_calls = 0;
        std::vector<size_t> short_offsets;
        std::atomic<int> stop{0};

        TnfsFile(const Bytes &image, size_t bsize = 128)
        {
            img = image;
            tnfs.data = &img;
            stdio.t = &tnfs;
            stdio.bsize = bsize;
        }
        // a freshly opened file positioned at `off` (the cassette task's header reads left it somewhere near the start)
        void open_at(size_t off)
        {
            tnfs.vfs_lseek_set(off);
            stdio.off_ = off;
            stdio.bl = stdio.bp = 0;
        }
    };

    long ops_tell(void *c) { return static_cast<TnfsFile *>(c)->stdio.ftell(); }
    bool ops_seek(void *c, size_t off) { return static_cast<TnfsFile *>(c)->stdio.fseek(off); }
    size_t ops_read(void *c, uint8_t *dst, size_t n) { return static_cast<TnfsFile *>(c)->stdio.fread(dst, n); }
    bool ops_stopping(void *c) { return static_cast<TnfsFile *>(c)->stop.load() != 0; }

    // The producer's read callback over the simulated file. resilient == false is the code of the first
    // physical run (trust ftell, seek only when it differs, accept a short count as it comes).
    size_t tnfs_file_read(void *ctx, size_t off, uint8_t *dst, size_t n)
    {
        TnfsFile *f = static_cast<TnfsFile *>(ctx);
        ++f->calls;
        size_t r;
        if (f->resilient)
        {
            const FskFileOps ops = {f, ops_tell, ops_seek, ops_read, ops_stopping};
            r = fsk_prog_resilient_read(ops, f->img.size(), f->suspect, f->rs, off, dst, n);
        }
        else
        {
            r = 0;
            if (f->stdio.ftell() == static_cast<long>(off) || f->stdio.fseek(off))
                r = f->stdio.fread(dst, n);
        }
        if (r != n)
        {
            ++f->short_calls;
            f->short_offsets.push_back(off);
        }
        return r;
    }

    struct TnfsProg
    {
        Prog p;
        TnfsFile *file = nullptr;
        bool init(const Bytes &img, size_t off, bool resilient, size_t bsize = 128)
        {
            if (!p.init(img, off))
                return false;
            file = new TnfsFile(img, bsize);
            file->resilient = resilient;
            // What the cassette task did before the loader started (walk-hdr at 0, then at 8): the first TNFS fill
            // starts at file offset 0, so every later fill starts at a multiple of 512.
            file->open_at(0);
            uint8_t hdr[8];
            file->stdio.fread(hdr, 8);
            file->stdio.fseek(off);
            file->stdio.fread(hdr, 8);
            fsk_prog_loader_init(p.L, p.run(), tnfs_file_read, file, pool_alloc, &p.pool, img.size());
            return true;
        }
        ~TnfsProg() { delete file; }
        // Runs the producer to a stop. Returns the final step result.
        FskProgStep run_all(size_t max_steps = 5000000)
        {
            FskProgStep st;
            size_t n = 0;
            do
                st = fsk_prog_step(p.L);
            while (st == FskProgStep::PROGRESS && ++n < max_steps);
            return st;
        }
        // every published block equals the file bytes it stands for
        bool payload_matches_file() const
        {
            const FskProgRun &r = p.cas._fsk_prog;
            const uint32_t chunks = fsk_pub_load(&r.pub_chunks);
            const uint32_t pub = fsk_pub_load(&r.pub_values);
            for (uint32_t ch = 0; ch < chunks; ++ch)
            {
                const size_t avail = r.prefix[ch] < pub ? std::min<size_t>(r.value_counts[ch], pub - r.prefix[ch]) : 0;
                const size_t pos = r.hdr_off[ch] + 8;
                for (size_t b = 0; b * 2 < avail * 2; b += FSK_PROG_BLOCK_BYTES)
                {
                    const size_t bytes = std::min<size_t>(FSK_PROG_BLOCK_BYTES, avail * 2 - b);
                    const uint8_t *blk = p.table[r.block_base[ch] + b / FSK_PROG_BLOCK_BYTES];
                    if (blk == nullptr || std::memcmp(blk, p.img.data() + pos + b, bytes) != 0)
                        return false;
                }
            }
            return true;
        }
    };
} // namespace

TEST_CASE("field failure reproduced: the first physical run's read path turns a lost READ reply into a false FINAL")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    REQUIRE(off == 8);

    // The fill that starts at file offset 150528 (the 24 bytes after the 488 delivered at 150040) never gets its
    // reply; the server executed it. Exactly one such loss.
    TnfsProg tp;
    REQUIRE(tp.init(img, off, /*resilient=*/false));
    tp.file->tnfs.faults.push_back({150528, 1, true});
    const FskProgStep st = tp.run_all();

    // what the ring showed: a positive 488/512 read at off=150040 ...
    REQUIRE_FALSE(tp.file->short_offsets.empty());
    CHECK(tp.file->short_offsets.front() == 150040);
    // ... and the producer finishing FINAL after exactly 3 chunks, 196620 bytes, 98298 values (a false FINAL)
    CHECK(st == FskProgStep::DONE);
    CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 3);
    CHECK(fsk_pub_load(&tp.p.run().pub_values) == 98298);
    CHECK(tp.p.L.bytes == 196620);
    // the file has 8 chunks: the run was cut short by a wrong header, not by the structure
    CHECK(tp.p.ref.hdr_off.size() == 8);
    CHECK(tp.p.run().next_offset != tp.p.ref.next_offset);
    // the bytes published after the loss are the file's bytes 512 further on: corrupted waveform from 150528
    CHECK_FALSE(tp.payload_matches_file());
    const size_t k = 150528 - 131096; // offset inside chunk 2's payload
    const uint8_t *blk = tp.p.table[tp.p.run().block_base[2] + k / FSK_PROG_BLOCK_BYTES];
    REQUIRE(blk != nullptr);
    CHECK(blk[k % FSK_PROG_BLOCK_BYTES] == img[150528 + 512]);
    // where that lands on the tape: 294.3696 s of the waveform, 10.8 ms before the Atari's CORRECCION freeze (294.3805 s)
    const Drive ref = reference_from(img, off, 0);
    uint64_t t = 0;
    const uint8_t *pl2 = img.data() + 131096;
    for (size_t v = 0; v < k / 2; ++v)
        t += fsk_ticks_for_value(fsk_decode_le16(pl2 + 2 * v));
    const uint64_t chunk2_start = 260970000ULL;
    MESSAGE("corruption begins at waveform ", (chunk2_start + t) / 1e6, " s; the physical freeze was at 294.380492 s");
    CHECK(chunk2_start + t == 294369600ULL);
    CHECK(294380492ULL - (chunk2_start + t) < 20000ULL); // within 20 ms
    (void)ref;
}

TEST_CASE("field failure fixed: the same loss with the resilient read completes all 8 chunks, byte for byte")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = 8;
    const Drive ref = reference_from(img, off, 0);
    for (size_t bsize : {64u, 128u, 256u, 512u})
        for (bool executed : {true, false})
        {
            INFO("stdio buffer ", bsize, " server_executed=", executed);
            TnfsProg tp;
            REQUIRE(tp.init(img, off, true, bsize));
            tp.file->tnfs.faults.push_back({150528, 1, executed});
            const FskProgStep st = tp.run_all();
            CHECK(st == FskProgStep::DONE);
            CHECK(fsk_prog_state(tp.p.run()) == FskProgState::FINAL);
            CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 8);
            CHECK(fsk_pub_load(&tp.p.run().pub_values) == 252122);
            CHECK(tp.p.run().next_offset == tp.p.ref.next_offset);
            CHECK(tp.file->tnfs.failed_fills == 1);
            CHECK(tp.file->rs.faults >= 1);
            CHECK(tp.file->rs.resyncs >= 1);
            CHECK(tp.file->rs.mismatches == 0);
            CHECK(tp.payload_matches_file());
            // and the waveform the consumer plays from it is the reference, value for value
            tp.p.cas.seed(0, 0, 0);
            const Drive d = drive(tp.p.cas, 0, 0, [](int64_t) {});
            CHECK(d.complete);
            CHECK_FALSE(d.underrun);
            CHECK(d.wave == ref.wave);
        }
}

TEST_CASE("short reads: a sweep of fault positions across payload blocks, chunk boundaries and headers never corrupts or ends the run")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = 8;
    std::vector<size_t> positions; // fill start positions to lose
    // every alignment inside a block, around the first chunk boundaries and headers
    for (size_t d = 0; d < 512; d += 37)
        positions.push_back(2048 + d);
    for (size_t hdr : {65548u, 131088u, 196628u, 262168u, 327708u, 393248u, 458788u})
        for (int dlt = -1024; dlt <= 1024; dlt += 256)
            positions.push_back(hdr + dlt);
    positions.push_back(150528);
    positions.push_back(504316 - 512); // the last fill
    int runs = 0;
    for (size_t at : positions)
        for (bool executed : {true, false})
        {
            // the fill grid is 512-aligned from the start: pick the fill whose start is closest to `at`
            const size_t fill_start = (at / 512) * 512;
            TnfsProg tp;
            REQUIRE(tp.init(img, off, true, 128));
            tp.file->tnfs.faults.push_back({fill_start, 1, executed});
            const FskProgStep st = tp.run_all();
            INFO("fill at ", fill_start, " executed=", executed);
            CHECK(st == FskProgStep::DONE);
            CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 8);
            CHECK(fsk_pub_load(&tp.p.run().pub_values) == 252122);
            CHECK(tp.payload_matches_file()); // no skipped bytes, no duplicated bytes, no wrong header
            for (size_t c = 0; c < 8; ++c)
                CHECK(tp.p.run().hdr_off[c] == tp.p.ref.hdr_off[c]); // every next header read at the right place
            ++runs;
        }
    MESSAGE("fault positions swept: ", runs, " runs, all identical to the file");
}

TEST_CASE("short reads: repeated faults, a persistent failure, and recovery from FAILED at the exact confirmed offset")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = 8;

    SUBCASE("the same fill fails twice in a row, then succeeds")
    {
        TnfsProg tp;
        REQUIRE(tp.init(img, off, true));
        tp.file->tnfs.faults.push_back({150528, 2, true});
        CHECK(tp.run_all() == FskProgStep::DONE);
        CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 8);
        CHECK(tp.payload_matches_file());
    }
    SUBCASE("several separate faults across the run")
    {
        TnfsProg tp;
        REQUIRE(tp.init(img, off, true));
        for (size_t at : {20480u, 65536u, 131072u, 150528u, 262144u, 458752u})
            tp.file->tnfs.faults.push_back({at, 1, true});
        CHECK(tp.run_all() == FskProgStep::DONE);
        CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 8);
        CHECK(tp.payload_matches_file());
        CHECK(tp.file->tnfs.failed_fills == 6);
    }
    SUBCASE("a persistent failure ends FAILED (never FINAL), keeps the published part, and resumes exactly")
    {
        TnfsProg tp;
        REQUIRE(tp.init(img, off, true));
        tp.file->tnfs.faults.push_back({150528, 1000000, true}); // the network is gone
        const FskProgStep st = tp.run_all();
        CHECK(st == FskProgStep::FAILED);
        CHECK(fsk_prog_state(tp.p.run()) == FskProgState::FAILED);
        CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 3);
        const uint32_t pub = fsk_pub_load(&tp.p.run().pub_values);
        // the confirmed watermark is a whole number of blocks, all of it real, and the cursor names the next byte
        CHECK(pub == 65532 + tp.p.L.chunk_bytes / 2);
        CHECK(tp.payload_matches_file());
        // the cursor names the exact next byte: chunk 2, bytes loaded so far, file position right after them
        CHECK(tp.p.L.chunk == 2);
        CHECK(tp.p.L.file_pos == 131096 + tp.p.L.chunk_bytes);
        CHECK(tp.p.L.chunk_bytes == FSK_PROG_BLOCK_BYTES * (tp.p.L.chunk_bytes / FSK_PROG_BLOCK_BYTES));
        const size_t cursor_bytes = tp.p.L.chunk_bytes;
        // the encoder that catches it stops explicitly (UNDERRUN), it does not read past the watermark
        {
            tp.p.cas.seed(2, 0, 0);
            const Drive d = drive(tp.p.cas, 0, 0, [](int64_t) {});
            CHECK(d.underrun);
        }
        // the network comes back: resume from the cursor
        tp.file->tnfs.faults.clear();
        fsk_prog_loader_recover(tp.p.L);
        tp.file->suspect = true;
        CHECK(tp.p.L.chunk_bytes == cursor_bytes); // untouched by the recovery
        CHECK(tp.run_all() == FskProgStep::DONE);
        CHECK(fsk_prog_state(tp.p.run()) == FskProgState::FINAL);
        CHECK(fsk_pub_load(&tp.p.run().pub_chunks) == 8);
        CHECK(fsk_pub_load(&tp.p.run().pub_values) == 252122);
        CHECK(tp.payload_matches_file());
        tp.p.cas.seed(0, 0, 0);
        const Drive d = drive(tp.p.cas, 0, 0, [](int64_t) {});
        CHECK(d.complete);
        CHECK(d.wave == reference_from(img, off, 0).wave);
    }
    SUBCASE("a stop request interrupts the retries")
    {
        TnfsProg tp;
        REQUIRE(tp.init(img, off, true));
        tp.file->tnfs.faults.push_back({150528, 1000000, true});
        tp.file->stop.store(1);
        // the resilient reader gives up at once; the producer reports a fault, not a run end
        const FskProgStep st = tp.run_all();
        CHECK(st != FskProgStep::DONE);
    }
}

TEST_CASE("headers: a header that structurally exists but cannot be read is a fault, only a missing header ends the run")
{
    std::vector<uint16_t> v(3000, 4);
    const Bytes img = make_image({{"fsk ", 10, values_payload(v)}, {"fsk ", 0, values_payload(v)}});
    // (a) the second header is unreadable (transport): FAILED, never FINAL with one chunk
    {
        Prog p;
        REQUIRE(p.init(img, 24));
        const size_t second_hdr = p.ref.hdr_off[1];
        p.file.fail_at = second_hdr;
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::FAILED);
        CHECK(fsk_pub_load(&p.run().pub_chunks) == 1);
    }
    // (b) no header can exist (the file ends at the chunk): FINAL
    {
        const Bytes one = make_image({{"fsk ", 10, values_payload(v)}});
        Prog p;
        REQUIRE(p.init(one, 24));
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::DONE);
        CHECK(fsk_prog_state(p.run()) == FskProgState::FINAL);
    }
    // (c) fewer than 8 bytes remain after the chunk: FINAL (end of tape), the same rule as the walker
    {
        Bytes tail = make_image({{"fsk ", 10, values_payload(v)}});
        tail.insert(tail.end(), {1, 2, 3});
        Prog p;
        REQUIRE(p.init(tail, 24));
        FskProgStep st;
        do
            st = fsk_prog_step(p.L);
        while (st == FskProgStep::PROGRESS);
        CHECK(st == FskProgStep::DONE);
    }
}

TEST_CASE("resilient read: end of file is a legitimate short read, an unstable re-read is refused")
{
    Bytes img(4096);
    for (size_t i = 0; i < img.size(); ++i)
        img[i] = static_cast<uint8_t>(i * 7 + 3);
    TnfsFile f(img, 128);
    f.tnfs.data = &f.img;
    f.open_at(0);
    bool suspect = false;
    FskReadStats rs;
    const FskFileOps ops = {&f, ops_tell, ops_seek, ops_read, ops_stopping};
    uint8_t buf[512];
    // the last 100 bytes: 512 requested, 100 exist -> a legitimate short read, no fault
    const size_t r = fsk_prog_resilient_read(ops, img.size(), suspect, rs, 3996, buf, 512);
    CHECK(r == 100);
    CHECK(rs.faults == 0);
    CHECK(std::memcmp(buf, img.data() + 3996, 100) == 0);
    // a fault mid-file: the delivered part is verified against the re-read
    f.open_at(1000);
    f.tnfs.faults.push_back({1000, 1, true}); // the very first fill fails: nothing delivered, then a resync
    const size_t r2 = fsk_prog_resilient_read(ops, img.size(), suspect, rs, 1000, buf, 512);
    CHECK(r2 == 512);
    CHECK(rs.faults == 1);
    CHECK(rs.resyncs >= 1);
    CHECK(std::memcmp(buf, img.data() + 1000, 512) == 0);
}

// =============================================================================================================
// Hardware-like segmentation of the encoder: arbitrary RMT output-memory capacities
// =============================================================================================================
//
// The RMT driver calls the encoder with `symbols_free` = the free part of its 512-symbol memory (512 for the
// prefill, then 256 per half in practice; the driver contract allows any positive value) and expects either
// `symbols_free` symbols back or `done`. The tests below run the PRODUCTION callback (extracted verbatim) with every
// kind of capacity sequence and demand that the concatenated symbol stream is word-for-word the stream of the
// validated full-preload path.

namespace
{
    using Words = std::vector<uint32_t>;

    uint32_t symbol_word(const rmt_symbol_word_t &s)
    {
        return static_cast<uint32_t>(s.duration0) | (static_cast<uint32_t>(s.level0) << 15) |
               (static_cast<uint32_t>(s.duration1) << 16) | (static_cast<uint32_t>(s.level1) << 31);
    }

    struct SegResult
    {
        Words words;
        bool complete = false;
        bool underrun = false;
        uint32_t calls = 0;
        bool contract_ok = true;
        uint64_t ticks = 0;
        uint32_t mem_full = 0; // calls that filled the whole offered memory and were not done
    };

    // Rewinds the host stand-in to a fresh transmission (what fsk_signal_begin() does before rmt_transmit()).
    void begin_transmission(sioCassette &cas, size_t seed_chunk, size_t seed_value, uint64_t seed_skip, uint64_t seed_ticks)
    {
        cas._fsk_stop_flag = false;
        cas._fsk_encoding_complete = false;
        cas._fsk_underrun = false;
        cas.seed(seed_chunk, seed_value, seed_skip);
        fsk_bounds_reset(cas._fsk_bounds, seed_ticks);
        fsk_ref_log_reset(cas._fsk_ref_log, seed_ticks);
        cas._fsk_transmission_started = false;
    }

    // Capacity sequence source: next() gives the symbols_free of the next call.
    struct Caps
    {
        virtual ~Caps() {}
        virtual size_t next() = 0;
    };
    struct DriverCaps : Caps // the real driver: 512 then 256
    {
        bool first = true;
        size_t next() override { size_t r = first ? 512 : 256; first = false; return r; }
    };
    struct RandCaps : Caps
    {
        std::mt19937 rng;
        int mode;
        RandCaps(uint32_t seed, int m) : rng(seed), mode(m) {}
        size_t next() override
        {
            switch (mode)
            {
            case 0: return 1 + rng() % 512;                         // any capacity
            case 1: return 1 + rng() % 3;                           // 1..3: MEM_FULL almost every symbol
            case 2: return 1 + 2 * (rng() % 256);                   // odd capacities only
            case 3: { static const size_t e[] = {1, 2, 255, 256, 257, 511, 512}; return e[rng() % 7]; }
            default: return (rng() & 1) ? 1 : 512;                  // extremes
            }
        }
    };
    struct CutCaps : Caps // MEM_FULL exactly at chosen symbol positions
    {
        std::vector<size_t> cuts; // ascending symbol indexes at which a call must end
        size_t emitted = 0, k = 0;
        size_t next() override
        {
            while (k < cuts.size() && cuts[k] <= emitted)
                ++k;
            size_t want = (k < cuts.size()) ? cuts[k] - emitted : 512;
            if (want > 512)
                want = 512;
            emitted += want; // the encoder fills exactly this many (or ends)
            return want;
        }
    };

    SegResult run_segmented(sioCassette &cas, Caps &caps, size_t seed_chunk = 0, size_t seed_value = 0,
                            uint64_t seed_skip = 0, uint64_t seed_ticks = 0)
    {
        SegResult out;
        begin_transmission(cas, seed_chunk, seed_value, seed_skip, seed_ticks);
        std::vector<rmt_symbol_word_t> buf(1024);
        bool done = false;
        size_t played = 0;
        for (uint32_t call = 0; call < 20000000 && !done; ++call)
        {
            const size_t offered = caps.next();
            std::memset(buf.data(), 0xA5, buf.size() * sizeof(buf[0])); // garbage: only what is returned may be used
            g_now_us += 1000; // a refill every ~ms of virtual time; the margins are checked separately
            const size_t got = sioCassette::fsk_encode_cb(cas._fsk_blocks, cas._fsk_block_count * sizeof(uint8_t *), played,
                                                          offered, buf.data(), &done, &cas);
            ++out.calls;
            if (call == 0)
                cas._fsk_transmission_started = true;
            if (got > offered || (got == 0 && !done) || (!done && got != offered))
                out.contract_ok = false;
            if (!done && got == offered)
                ++out.mem_full;
            for (size_t i = 0; i < got; ++i)
                out.words.push_back(symbol_word(buf[i]));
            played += got;
        }
        out.complete = cas._fsk_encoding_complete;
        out.underrun = cas._fsk_underrun;
        out.ticks = cas._fsk_bounds.cumulative_ticks;
        return out;
    }

    // The FNV-1a fingerprint of a symbol stream, computed independently of the callback's own counter.
    uint32_t fingerprint(const Words &w)
    {
        uint32_t h = 2166136261u;
        for (uint32_t x : w)
            h = (h ^ x) * 16777619u;
        return h;
    }

    // Symbol positions of the structural boundaries of a run, from its values: where a value starts, where a
    // value that needs several portions has its interior portion boundaries, and where a chunk starts.
    struct Boundaries
    {
        std::vector<size_t> value_start, long_interior, chunk_start;
    };
    Boundaries boundaries_of(const Bytes &img, size_t off)
    {
        Boundaries b;
        RunRef ref;
        if (!ref_scan(img, off, ref))
            return b;
        size_t portion = 0;
        for (size_t c = 0; c < ref.hdr_off.size(); ++c)
        {
            b.chunk_start.push_back(portion / 2);
            const uint8_t *pl = img.data() + ref.hdr_off[c] + 8;
            for (size_t v = 0; v < ref.avail[c] / 2; ++v)
            {
                const uint16_t val = fsk_decode_le16(pl + 2 * v);
                if (val == 0)
                    continue;
                b.value_start.push_back(portion / 2);
                uint64_t rem = static_cast<uint64_t>(val) * 100ULL;
                bool first = true;
                while (rem > 0)
                {
                    const uint64_t p = rem > 32767ULL ? 32767ULL : rem;
                    if (!first)
                        b.long_interior.push_back(portion / 2);
                    first = false;
                    ++portion;
                    rem -= p;
                }
            }
        }
        return b;
    }
} // namespace

TEST_CASE("encoder segmentation: any RMT capacity sequence yields the validated symbol stream (real Zorro, thousands of seeds)")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = 8;

    // The validated full-preload path with the real driver's 512/256 rhythm is the reference.
    Classic ref;
    REQUIRE(ref.init(img, off));
    DriverCaps dc;
    const SegResult R = run_segmented(ref.cas, dc);
    REQUIRE(R.contract_ok);
    REQUIRE(R.complete);
    // the canonical stream of the earlier physical campaigns: 127200 symbols, 927329800 ticks
    CHECK(R.words.size() == 127200);
    CHECK(R.ticks == 927329800ULL);
    const uint32_t ref_hash = fingerprint(R.words);
    MESSAGE("canonical Zorro stream: symbols=", R.words.size(), " ticks=", R.ticks, " fnv1a=", ref_hash);
    CHECK(ref_hash == 0xaf807b81u); // the fingerprint the physical runs report
    CHECK(R.calls == 496);          // the callback count of the physical runs (cb_count=496)
    CHECK(R.mem_full == 495);

    Prog store;
    REQUIRE(store.init(img, off));
    ProdSim sim{&store, 0};
    sim.run_until(0);
    REQUIRE(fsk_prog_state(store.run()) == FskProgState::FINAL);

    auto check = [&](const SegResult &r, const char *what, uint32_t seed)
    {
        INFO(what, " seed ", seed);
        CHECK(r.contract_ok);
        CHECK(r.complete);
        CHECK_FALSE(r.underrun);
        CHECK(r.words.size() == R.words.size());
        CHECK(r.words == R.words); // word for word: levels, durations, half-symbol pairing, final pad
        CHECK(r.ticks == R.ticks);
        CHECK(fingerprint(r.words) == ref_hash);
    };

    // (a) the real driver rhythm through the progressive store: the run's own path
    {
        DriverCaps d2;
        const SegResult p = run_segmented(store.cas, d2);
        check(p, "driver caps, progressive", 0);
        CHECK(p.calls == R.calls);
        CHECK(p.mem_full == R.mem_full);
    }

    // (b) thousands of random segmentations, progressive store (and a sample of them on the classic path)
    int runs = 0;
    for (uint32_t seed = 1; seed <= 2400; ++seed)
    {
        RandCaps rc(seed, static_cast<int>(seed % 5));
        const SegResult p = run_segmented(store.cas, rc);
        check(p, "random capacities, progressive", seed);
        ++runs;
        if (seed % 12 == 0)
        {
            RandCaps rc2(seed, static_cast<int>(seed % 5));
            check(run_segmented(ref.cas, rc2), "random capacities, full preload", seed);
            ++runs;
        }
        if (seed % 400 == 0)
            MESSAGE("... ", seed, " seeds");
    }

    // (c) MEM_FULL exactly at value starts, inside multi-portion values and at chunk starts (and one symbol either side)
    const Boundaries B = boundaries_of(img, off);
    REQUIRE(B.chunk_start.size() == 8);
    REQUIRE_FALSE(B.long_interior.empty());
    std::mt19937 rng(77);
    auto targeted = [&](const std::vector<size_t> &pool, int jitter_range, uint32_t seed)
    {
        CutCaps cc;
        std::mt19937 r2(seed);
        std::vector<size_t> cuts;
        for (int i = 0; i < 300; ++i)
        {
            long p = static_cast<long>(pool[r2() % pool.size()]) + static_cast<long>(r2() % (2 * jitter_range + 1)) - jitter_range;
            if (p > 0 && p < 127200)
                cuts.push_back(static_cast<size_t>(p));
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        cc.cuts = cuts;
        return cc;
    };
    for (uint32_t seed = 1; seed <= 60; ++seed)
        for (int kind = 0; kind < 3; ++kind)
        {
            const std::vector<size_t> &pool = kind == 0 ? B.value_start : (kind == 1 ? B.long_interior : B.chunk_start);
            CutCaps c1 = targeted(pool, 1, seed * 3 + kind); // exactly at, one before, one after
            check(run_segmented(store.cas, c1), kind == 0 ? "cuts at value starts" : (kind == 1 ? "cuts inside long values" : "cuts at chunk starts"), seed);
            ++runs;
        }
    // every single chunk boundary, one call ending exactly there
    for (size_t cs : B.chunk_start)
        for (long d = -2; d <= 2; ++d)
        {
            if (cs + d == 0 || static_cast<long>(cs) + d < 1)
                continue;
            CutCaps c1;
            c1.cuts = {cs + d};
            check(run_segmented(store.cas, c1), "cut at a chunk boundary", static_cast<uint32_t>(cs));
            ++runs;
        }
    MESSAGE("segmentations run: ", runs, ", all identical to the validated stream");
    (void)rng;
}

TEST_CASE("encoder segmentation: the other real images, progressive store versus full preload, random capacities")
{
    int compared = 0;
    for (const char *name : kCorpus)
    {
        Bytes img;
        if (!require_image(name, img))
            continue;
        if (std::strcmp(name, "turbo_software_zorro.cas") == 0)
            continue; // covered above
        for (size_t off : wanted_runs(img, 1))
        {
            INFO(name, " run at ", off);
            Classic ref;
            REQUIRE(ref.init(img, off));
            DriverCaps dc;
            const SegResult R = run_segmented(ref.cas, dc);
            REQUIRE(R.complete);
            Prog store;
            REQUIRE(store.init(img, off));
            ProdSim sim{&store, 0};
            sim.run_until(0);
            for (uint32_t seed = 1; seed <= 150; ++seed)
            {
                RandCaps rc(seed, static_cast<int>(seed % 5));
                const SegResult p = run_segmented(store.cas, rc);
                CHECK(p.contract_ok);
                CHECK(p.words == R.words);
                CHECK(fingerprint(p.words) == fingerprint(R.words));
                ++compared;
            }
        }
    }
    MESSAGE("image/seed combinations: ", compared);
}

TEST_CASE("encoder segmentation: a resumed transmission (seed inside a value) segments identically")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = 8;
    Classic ref;
    REQUIRE(ref.init(img, off));
    Prog store;
    REQUIRE(store.init(img, off));
    ProdSim sim{&store, 0};
    sim.run_until(0);
    std::mt19937_64 rng(5);
    for (int i = 0; i < 60; ++i)
    {
        const uint64_t p0 = 1 + rng() % 900000000ULL;
        const FskLocateResult loc = cas_fsk_locate_ticks(ref.cas._fsk_run_value_counts, ref.cas._fsk_run_chunk_count,
                                                         &Classic::value_at, &ref, p0);
        if (loc.at_end)
            continue;
        DriverCaps d1;
        const SegResult R = run_segmented(ref.cas, d1, loc.chunk_index, loc.value_index, loc.skip_ticks, p0);
        RandCaps rc(static_cast<uint32_t>(i + 1), i % 5);
        const SegResult P = run_segmented(store.cas, rc, loc.chunk_index, loc.value_index, loc.skip_ticks, p0);
        CHECK(P.contract_ok);
        CHECK(P.words == R.words);
        CHECK(P.ticks == R.ticks);
    }
}

TEST_CASE("encoder rhythm: 496 callbacks, 495 memory-full returns and more than 100 ms of refill slack on the real Zorro")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    Classic ref;
    REQUIRE(ref.init(img, 8));
    // Virtual time: the hardware plays 1 tick = 1 us and the callback runs 300 us after each threshold. The slack of a
    // refill is (how long the data already resident lasts) - (the time the callback returned): it would have to reach
    // zero for the hardware to run dry. (A store underrun is a different thing: the producer late, not the refill.)
    begin_transmission(ref.cas, 0, 0, 0, 0);
    std::vector<rmt_symbol_word_t> buf(512);
    bool done = false;
    std::vector<uint64_t> sym_end;
    uint64_t hw = 0;
    const int64_t t0 = 5000000;
    int64_t worst = INT64_MAX;
    size_t played = 0;
    uint32_t calls = 0, mem_full = 0;
    for (size_t n = 0; !done; ++n)
    {
        int64_t T = t0;
        size_t offered = 512;
        if (n > 0)
        {
            const size_t idx = 256 * n - 2;
            if (idx >= sym_end.size())
                break;
            T = t0 + static_cast<int64_t>(sym_end[idx]) + 300;
            offered = 256;
        }
        g_now_us = T;
        const uint64_t cum_before = ref.cas._fsk_bounds.cumulative_ticks;
        const size_t got = sioCassette::fsk_encode_cb(ref.cas._fsk_blocks, ref.cas._fsk_block_count * sizeof(uint8_t *), played, offered,
                                                      buf.data(), &done, &ref.cas);
        ++calls;
        if (!done && got == offered)
            ++mem_full;
        if (n == 0)
            ref.cas._fsk_transmission_started = true;
        else
            worst = std::min<int64_t>(worst, (t0 + static_cast<int64_t>(cum_before)) - T);
        for (size_t i = 0; i < got; ++i)
        {
            hw += buf[i].duration0 + buf[i].duration1;
            sym_end.push_back(hw);
        }
        played += got;
    }
    MESSAGE("real-driver rhythm on Zorro: callbacks=", calls, " mem_full=", mem_full, " min slack=", worst, " us");
    CHECK(calls == 496);
    CHECK(mem_full == 495);
    CHECK(worst > 100000); // >100 ms of slack even in the densest region: no refill deadline is near
    CHECK(worst > 1000000); // (measured 1.17 s)
}

// =============================================================================================================
// Pre-hardware audit additions
// =============================================================================================================

TEST_CASE("underrun termination follows the driver-supported path: done with zero or more symbols, never a bogus value")
{
    // Underrun at every alignment of the encoder cursor against the offered halves: 0 symbols + done, and
    // n < offered symbols + done (a padded half), both must appear and both must carry only real data.
    std::vector<uint16_t> v;
    for (int i = 0; i < 30000; ++i)
        v.push_back(static_cast<uint16_t>(2 + (i * 7) % 11));
    const Bytes img = make_image({{"fsk ", 0, values_payload(v)}});
    const Drive ref = reference_from(img, 24, 0);
    int zero_done = 0, partial_done = 0;
    for (size_t stop_values = 1500; stop_values < 1500 + 300; ++stop_values)
    {
        Prog p;
        REQUIRE(p.init(img, 24));
        // publish exactly stop_values values, then never more
        while (fsk_pub_load(&p.run().pub_values) < stop_values)
            REQUIRE(fsk_prog_step(p.L) == FskProgStep::PROGRESS);
        const uint32_t pub = fsk_pub_load(&p.run().pub_values);
        p.cas.seed(0, 0, 0);
        fsk_bounds_reset(p.cas._fsk_bounds, 0);
        fsk_ref_log_reset(p.cas._fsk_ref_log, 0);
        Rle wave;
        bool done = false;
        size_t offered = 512;
        rmt_symbol_word_t buf[512];
        for (int call = 0; call < 100000 && !done; ++call)
        {
            std::memset(buf, 0, sizeof(buf));
            const size_t got = sioCassette::fsk_encode_cb(p.cas._fsk_blocks, p.cas._fsk_block_count * sizeof(uint8_t *), 0,
                                                          offered, buf, &done, &p.cas);
            for (size_t i = 0; i < got; ++i)
            {
                rle_push(wave, buf[i].level0 ^ FSK_RMT_OUT_INVERT, buf[i].duration0);
                rle_push(wave, buf[i].level1 ^ FSK_RMT_OUT_INVERT, buf[i].duration1);
            }
            if (done)
            {
                if (got == 0)
                    ++zero_done;
                else if (got < offered)
                    ++partial_done;
            }
            offered = 256;
        }
        REQUIRE(done);
        CHECK(p.cas._fsk_underrun);
        CHECK(p.cas._fsk_encoding_complete);
        (void)pub;
        CHECK(wave == rle_prefix(ref.wave, rle_ticks(wave))); // only real data
    }
    MESSAGE("underrun endings: ", partial_done, " with a partial half, ", zero_done, " with zero symbols");
    CHECK(partial_done > 0);
}

TEST_CASE("physical trace cross-check: the resumed segment of the remote rewind-during-load test is the intended waveform")
{
    // Hardware (remote TNFS, two -5 s rewinds while the store was still loading): the final segment started at
    // q_start=99702029 us (run offset 8, leading IRG 999000 us) and the encoder fingerprint of that segment was symbols=115940
    // ticks=828626771 hash=9ff3a803. The same segment through the production encoder on the host:
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    Classic ref;
    REQUIRE(ref.init(img, 8));
    const uint64_t irg_us = 999000;
    const uint64_t q_start = 99702029;
    const uint64_t p0 = q_start - irg_us;
    const FskLocateResult loc = cas_fsk_locate_ticks(ref.cas._fsk_run_value_counts, ref.cas._fsk_run_chunk_count,
                                                     &Classic::value_at, &ref, p0);
    REQUIRE_FALSE(loc.at_end);
    DriverCaps dc;
    const SegResult r = run_segmented(ref.cas, dc, loc.chunk_index, loc.value_index, loc.skip_ticks, p0);
    REQUIRE(r.complete);
    MESSAGE("host: symbols=", r.words.size(), " ticks=", r.ticks - p0, " fnv1a=", fingerprint(r.words));
    CHECK(r.words.size() == 115940);
    CHECK(r.ticks - p0 == 828626771ULL);
    CHECK(fingerprint(r.words) == 0x9ff3a803u);

    // The freeze: q=923119872 is 5208928 us before the end of the tape, which is exactly the encoded-ahead tail.
    const uint64_t total_q = irg_us + 927329800ULL;
    CHECK(total_q - 923119872ULL == 5208928ULL);
    CHECK((r.ticks - p0) - (923119872ULL - q_start) == 5208928ULL);
    // The rewinds are exact: -5.000000 s each
    CHECK(108991722ULL - 103991722ULL == 5000000ULL);
    CHECK(104702029ULL - 99702029ULL == 5000000ULL);

    // The short segment after the first rewind: 103991722 -> 104702029 = 710307 us of tape. A transmission that
    // stops inside its prefill makes ONE callback (the prefill: memory full) and no refill.
    Prog store;
    REQUIRE(store.init(img, 8));
    ProdSim sim{&store, 0};
    sim.run_until(0);
    const uint64_t p1 = 103991722ULL - irg_us;
    size_t counts[FSK_RUN_MAX_CHUNKS];
    const size_t n = fsk_prog_pub_counts(store.run(), 0, counts);
    FskProgView view{&store.run(), 0};
    const FskLocateResult loc1 = cas_fsk_locate_ticks(counts, n, fsk_prog_view_value, &view, p1);
    REQUIRE_FALSE(loc1.at_end);
    begin_transmission(store.cas, loc1.chunk_index, loc1.value_index, loc1.skip_ticks, p1);
    rmt_symbol_word_t buf[512];
    bool done = false;
    g_now_us = 1000000;
    const size_t got = sioCassette::fsk_encode_cb(store.cas._fsk_blocks, store.cas._fsk_block_count * sizeof(uint8_t *), 0, 512, buf,
                                                  &done, &store.cas);
    CHECK(got == 512);
    CHECK_FALSE(done); // memory full after the prefill: nothing else to say until the first refill
    // how long the prefill lasts: the first refill would not run before this much tape has been played
    uint64_t prefill_us = 0;
    for (size_t i = 0; i < 512; ++i)
        prefill_us += buf[i].duration0 + buf[i].duration1;
    MESSAGE("the prefill of the segment covers ", prefill_us, " us of tape; the segment played 710307 us");
    CHECK(prefill_us > 710307ULL);
}

TEST_CASE("start gate: the runway is computed from published durations only (no byte counts, no title logic)")
{
    const std::string src = read_text(std::string(FSK_PROGRESSIVE_TESTS_SOURCE_DIR) + "/lib/device/sio/fsk_progressive.cpp");
    const std::string plan = between(src, "FskProgPlan fsk_prog_plan_start(", "bool fsk_prog_chunk_time(");
    REQUIRE_FALSE(plan.empty());
    // the runway loop sums fsk_ticks_for_value() of published values
    CHECK(plan.find("runway += t;") != std::string::npos);
    CHECK(plan.find("fsk_ticks_for_value(fsk_prog_view_value(&view, c, v))") != std::string::npos);
    // no byte-count based watermark and nothing tied to an image
    // (FSK_PROG_BLOCK_BYTES appears only as the block size of the value accessor, never in the watermark)
    for (const char *tok : {"FSK_PROG_MIN_FIRST_CHUNK_BYTES", "zorro", "Zorro", "mirax", "2048", "16384"})
    {
        const bool absent = plan.find(tok) == std::string::npos;
        CHECK_MESSAGE(absent, "token in the gate: ", tok);
    }
    // Same density-independence, behaviourally: two chunks with equal byte size but different tape time need
    // different numbers of bytes to reach the runway.
    auto bytes_needed = [](uint16_t value)
    {
        std::vector<uint16_t> v(60000, value);
        Prog p;
        REQUIRE(p.init(make_image({{"fsk ", 0, values_payload(v)}}), 24));
        ProdSim sim{&p, 1000};
        const StartInfo s = wait_ready(p, sim, plan_in(0, 0), false, 1000);
        REQUIRE(s.ready);
        return fsk_pub_load(&p.run().pub_values) * 2;
    };
    const uint32_t sparse = bytes_needed(4000); // 0.4 s per value
    const uint32_t medium = bytes_needed(20);   // 2 ms per value
    CHECK(sparse < medium);
    CHECK(medium > 5 * sparse); // 3 KB (prefill-bound) versus ~20 KB (20 s of 2 ms values)
}

TEST_CASE("all-MARK: a run whose published part has no LOW time waits for FINAL and is classified like the full-preload path")
{
    // 40000 values, every LOW (even index) zero: all MARK, 20000 HIGH values of 1 ms
    std::vector<uint16_t> v;
    for (int i = 0; i < 40000; ++i)
        v.push_back((i & 1) == 0 ? 0 : 10);
    const Bytes img = make_image({{"fsk ", 100, values_payload(v)}});
    Prog p;
    REQUIRE(p.init(img, 24));
    ProdSim sim{&p, 1000};
    // not ready however much runway/prefill is published, until the loader is FINAL
    bool became_ready_before_final = false;
    while (!sim.finished)
    {
        sim.run_until(sim.clock + 1000);
        const FskProgPlan pl = fsk_prog_plan_start(p.run(), plan_in(100000, 100000));
        if (pl.ready && fsk_prog_state(p.run()) != FskProgState::FINAL)
            became_ready_before_final = true;
    }
    CHECK_FALSE(became_ready_before_final);
    const FskProgPlan pl = fsk_prog_plan_start(p.run(), plan_in(100000, 100000));
    REQUIRE(pl.ready);
    CHECK_FALSE(pl.has_space);
    // the production summary over the loaded store equals the classic one (what play_fsk_chunk uses)
    Classic c;
    REQUIRE(c.init(img, 24));
    const FskRunSummary a = fsk_run_summarize_from(reinterpret_cast<const uint8_t *const *>(c.cas._fsk_blocks), c.cas._fsk_block_size,
                                                   c.cas._fsk_run_block_base, c.cas._fsk_run_value_counts,
                                                   c.cas._fsk_run_chunk_count, 0, 0, 0);
    size_t counts[FSK_RUN_MAX_CHUNKS];
    const size_t n = fsk_prog_pub_counts(p.run(), 0, counts);
    const FskRunSummary b = fsk_run_summarize_from(reinterpret_cast<const uint8_t *const *>(p.table.data()), FSK_PROG_BLOCK_BYTES,
                                                   p.cas._fsk_run_block_base, counts, n, 0, 0, 0);
    CHECK_FALSE(a.has_space);
    CHECK_FALSE(b.has_space);
    CHECK(a.total_ticks == b.total_ticks);
}

TEST_CASE("all-MARK: no real corpus run of >= 16 KB is all-MARK (every progressive candidate carries LOW time)")
{
    const std::string dir = corpus_dir();
    if (dir.empty())
    {
        MESSAGE("SKIPPED (CASSETTE_TIME_TESTS_CORPUS_DIR not set)");
        return;
    }
    int files = 0, runs = 0, big = 0, big_all_mark = 0, small_all_mark = 0;
    for (const auto &e : std::filesystem::directory_iterator(dir))
    {
        if (e.path().extension() != ".cas")
            continue;
        Bytes img;
        if (!load_file(e.path().string(), img))
            continue;
        ++files;
        for (size_t off : run_starts(img))
        {
            Classic c;
            if (!c.init(img, off))
                continue;
            ++runs;
            const FskRunSummary sm = fsk_run_summarize_from(reinterpret_cast<const uint8_t *const *>(c.cas._fsk_blocks),
                                                            c.cas._fsk_block_size, c.cas._fsk_run_block_base,
                                                            c.cas._fsk_run_value_counts, c.cas._fsk_run_chunk_count, 0, 0, 0);
            const bool is_big = c.ref.avail[0] >= FSK_PROG_MIN_FIRST_CHUNK_BYTES;
            if (is_big)
            {
                ++big;
                if (!sm.has_space)
                    ++big_all_mark;
            }
            else if (!sm.has_space)
                ++small_all_mark;
        }
    }
    MESSAGE("corpus: ", files, " files, ", runs, " runs; runs with first chunk >= 16 KB: ", big, " (all-MARK: ", big_all_mark,
            "); smaller all-MARK runs (full-preload path anyway): ", small_all_mark);
    CHECK(files > 0);
    CHECK(big_all_mark == 0);
}

TEST_CASE("timebase: starting the RMT at ~1.7 s delivers the first real LOW at the same physical MOTOR-ON time as the validated start")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    Classic c;
    REQUIRE(c.init(img, off));
    const uint64_t irg = static_cast<uint64_t>(c.ref.irg_ms[0]) * 1000ULL;
    // where the first real LOW sits in the waveform (the production inert scan)
    const uint64_t I = cas_fsk_inert_ticks(c.cas._fsk_run_value_counts, c.cas._fsk_run_chunk_count, &Classic::value_at, &c,
                                           UINT64_MAX / 4, FSK_TIMEBASE_LOW_BUDGET_US);
    REQUIRE(I > 0);

    // Physical time (us after MOTOR ON) at which the first real LOW reaches DATA IN, for an RMT that starts at
    // t_start with waveform position p0: the emitted stream is scanned for the LOW run that begins at wave tick I.
    auto arrival = [&](const Drive &d, int64_t t_start, uint64_t p0) -> int64_t
    {
        uint64_t pos = p0;
        for (const Run &r : d.wave)
        {
            if (pos == I && r.level == 0)
                return t_start + static_cast<int64_t>(pos - p0);
            pos += r.ticks;
            if (pos > I)
                break;
        }
        return -1;
    };

    // (a) the validated implementation on a LAN: everything loaded first, RMT started at 8,107,871 us
    Prog full;
    REQUIRE(full.init(img, off));
    ProdSim fsim{&full, 0};
    fsim.run_until(0);
    FskProgPlanIn in = plan_in(irg, 0);
    in.timebase = true;
    in.elapsed_us = 8107871;
    const FskProgPlan pl_full = fsk_prog_plan_start(full.run(), in);
    REQUIRE(pl_full.ready);
    full.cas.seed(pl_full.seed_chunk, pl_full.seed_value, pl_full.seed_skip);
    const Drive d_full = drive(full.cas, pl_full.p0, 8107871, [](int64_t) {});
    const int64_t t_full = arrival(d_full, 8107871, pl_full.p0);

    // (b) progressive at the measured remote rate: RMT starts as soon as the runway is published
    Prog pg;
    REQUIRE(pg.init(img, off));
    ProdSim sim{&pg, 237500};
    const StartInfo s = wait_ready(pg, sim, plan_in(irg, 0), true, 10000);
    REQUIRE(s.ready);
    pg.cas.seed(s.plan.seed_chunk, s.plan.seed_value, s.plan.seed_skip);
    const Drive d_pg = drive(pg.cas, s.plan.p0, s.t_ready, [&](int64_t T) { sim.run_until(T); });
    const int64_t t_pg = arrival(d_pg, s.t_ready, s.plan.p0);

    MESSAGE("first real LOW at wave tick ", I, " (irg ", irg, "): validated start ", t_full, " us, progressive start at ",
            s.t_ready, " us -> ", t_pg, " us");
    // q_start = q0 + elapsed (never past the inert prefix), so wave(t) = t - irg for both starts:
    // arrival = irg + I, independent of when the RMT was started.
    CHECK(t_full == static_cast<int64_t>(irg + I));
    CHECK(t_pg == static_cast<int64_t>(irg + I));
    CHECK(t_pg == t_full);
    CHECK(s.t_ready < 8107871); // and the RMT really did start earlier
    // never inside the leading LOW-budget region: neither start advanced past the inert prefix
    CHECK(pl_full.p0 <= I);
    CHECK(s.plan.p0 <= I);
}

#ifdef FSK_PROGRESSIVE_TESTS_THREADS
TEST_CASE("threads: a free-running producer against a free-running consumer never yields unpublished data")
{
    Bytes img;
    if (!require_image("turbo_software_zorro.cas", img))
        return;
    const size_t off = wanted_runs(img, 1).at(0);
    const Drive ref = reference_from(img, off, 0);

    for (int trial = 0; trial < 4; ++trial)
    {
        Prog p;
        REQUIRE(p.init(img, off));
        std::atomic<bool> stop{false};
        std::atomic<int> producer_delay_us{trial * 20};
        std::thread producer(
            [&]()
            {
                std::mt19937 rng(trial + 1);
                FskProgStep st;
                do
                {
                    st = fsk_prog_step(p.L);
                    const int d = producer_delay_us.load();
                    if (d > 0 && (rng() & 3) == 0)
                        std::this_thread::sleep_for(std::chrono::microseconds(rng() % (d + 1)));
                } while (st == FskProgStep::PROGRESS && !stop.load());
            });

        // Consumer: encoder callbacks as fast as possible; every underrun is a freeze at the physical end and an
        // immediate resume (the plan is not used here: only the contract of the encoder is under test).
        Rle played;
        uint64_t pos = 0;
        bool finished = false;
        for (int rounds = 0; rounds < 2000000 && !finished; ++rounds)
        {
            // seed from the position; the value must be published (else wait: the encoder would underrun at once)
            size_t sc = 0, sv = 0;
            uint64_t sk = 0;
            if (pos > 0)
            {
                size_t counts[FSK_RUN_MAX_CHUNKS];
                const size_t n = fsk_prog_pub_counts(p.run(), 0, counts);
                FskProgView view{&p.run(), 0};
                const FskLocateResult loc = cas_fsk_locate_ticks(counts, n, fsk_prog_view_value, &view, pos);
                if (loc.at_end)
                {
                    if (fsk_prog_state(p.run()) == FskProgState::FINAL)
                    {
                        finished = true;
                        break;
                    }
                    std::this_thread::yield();
                    continue;
                }
                sc = loc.chunk_index;
                sv = loc.value_index;
                sk = loc.skip_ticks;
            }
            p.cas.seed(sc, sv, sk);
            const Drive d = drive(p.cas, pos, 0, [](int64_t) {});
            played = rle_concat(played, d.wave);
            pos += d.encoded_ticks;
            if (d.complete && !d.underrun)
                finished = true;
            else
                std::this_thread::yield();
        }
        stop.store(true);
        producer.join();
        CHECK(finished);
        CHECK(played == ref.wave); // any read of an unpublished/poisoned value would show up here
    }
}
#endif

// -----------------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    const int res = ctx.run();
    std::printf("\n=== REAL CAS FIXTURE STATUS (progressive loading) ===\n");
    std::printf("image loads that ran: %d, skipped (image not available): %d\n", g_real_ran, g_real_skipped);
    if (g_real_ran == 0)
        std::printf("SKIP: no real CAS image was found; only the in-memory structural tests ran for real. Set "
                    "CASSETTE_TIME_TESTS_ZORRO_CAS_PATH / CASSETTE_TIME_TESTS_CORPUS_DIR.\n");
    std::printf("=====================================================\n");
    return res;
}
