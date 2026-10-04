#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <atomic>
#include <thread>

#include "FSKTestImages.h"

using namespace fsktest;

// ------------------------------------------------------------------------------------------------
// Encoder: values to symbols (casFSK.h), the part the RMT refill callback runs
// ------------------------------------------------------------------------------------------------

namespace
{

std::vector<uint8_t> payload_of(const std::vector<uint16_t> &values)
{
    std::vector<uint8_t> p;
    for (uint16_t v : values)
    {
        p.push_back(static_cast<uint8_t>(v & 0xFF));
        p.push_back(static_cast<uint8_t>(v >> 8));
    }
    return p;
}

} // namespace

TEST_CASE("encoder: portions equal the independent oracle for any refill capacities")
{
    const std::vector<uint16_t> values = make_values(6000, 77);
    const std::vector<uint8_t> payload = payload_of(values);
    const FSKPreloadedValues source{payload.data(), values.size()};
    const std::vector<Portion> oracle = oracle_portions(values, SIZE_MAX);

    for (uint32_t seed = 1; seed <= 300; ++seed)
    {
        FSKNext stop = FSKNext::value;
        CHECK(encode_all(source, seed, stop) == oracle);
        CHECK(stop == FSKNext::end);
    }
}

TEST_CASE("encoder: a zero-duration value takes its parity slot and emits nothing")
{
    const std::vector<uint16_t> values = {0, 5, 0, 7, 3, 0, 4};
    const std::vector<uint8_t> payload = payload_of(values);
    const FSKPreloadedValues source{payload.data(), values.size()};

    FSKNext stop = FSKNext::value;
    const std::vector<Portion> out = encode_all(source, 1, stop);
    // value indexes 1 and 3 are high, 4 and 6 low
    REQUIRE(out.size() == 4);
    CHECK(out[0] == Portion{500, true});
    CHECK(out[1] == Portion{700, true});
    CHECK(out[2] == Portion{300, false});
    CHECK(out[3] == Portion{400, false});
}

TEST_CASE("encoder: a long value is split into portions of at most 32767 ticks, across refills")
{
    const std::vector<uint16_t> values = {2000, 1}; // 200000 ticks, then 100
    const std::vector<uint8_t> payload = payload_of(values);
    const FSKPreloadedValues source{payload.data(), values.size()};

    FSKEncodeState st;
    std::vector<Portion> out;
    FSKNext stop = FSKNext::value;
    while (stop == FSKNext::value)
    {
        Sym sym{};
        const size_t n = fsk_fill_symbols(st, source, &sym, 1, stop);
        if (n == 1)
        {
            out.push_back({sym.duration0, sym.level0});
            if (sym.duration1 != 0)
                out.push_back({sym.duration1, sym.level1});
        }
    }
    CHECK(stop == FSKNext::end);
    CHECK(out == oracle_portions(values, SIZE_MAX));
    CHECK(out.size() == 8); // 6 x 32767 + 3398, then 100
    CHECK(out[0].ticks == 32767);
    CHECK(out[6].ticks == 200000 - 6 * 32767);
}

TEST_CASE("encoder: an odd last portion ends its symbol without an extra edge")
{
    const std::vector<uint16_t> values = {3};
    const std::vector<uint8_t> payload = payload_of(values);
    const FSKPreloadedValues source{payload.data(), values.size()};

    FSKEncodeState st;
    Sym sym{};
    FSKNext stop = FSKNext::value;
    CHECK(fsk_fill_symbols(st, source, &sym, 4, stop) == 1);
    CHECK(stop == FSKNext::end);
    CHECK(sym.duration0 == 300);
    CHECK_FALSE(sym.level0);
    CHECK(sym.duration1 == 0);
    CHECK(sym.level1 == sym.level0);
}

// ------------------------------------------------------------------------------------------------
// Producer: what gets published, in what order, and where the run ends
// ------------------------------------------------------------------------------------------------

TEST_CASE("loader: publishes the run in order, block by block, joined chunks included")
{
    // 700 values per chunk = 1400 bytes: chunk boundaries fall inside blocks
    const Image img = make_image(4, 700, 11);
    Rig rig(img);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(ok); // monotonic, and every published value equals the oracle's
    CHECK(fsk_run_state(rig.run) == FSKLoaderState::final_run);
    CHECK(rig.run.pub_values == img.run_values.size());
    CHECK(rig.run.next_offset == img.run_end);
    CHECK(rig.run.total_ticks == total_ticks(oracle_portions(img.run_values, SIZE_MAX)));
}

TEST_CASE("loader: reads are sequential and stay inside the run")
{
    Image img = make_image(3, 1000, 5);
    img.add_header("data", 4, 0);
    img.add_values({1, 2}); // must never be read as payload
    Rig rig(img);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(rig.L.seeks == 0);
    // at most the header of the chunk that ends the run is read past the run
    CHECK(rig.file.high_water <= img.run_end + FSK_CHUNK_HEADER_BYTES);
    CHECK(rig.run.next_offset == img.run_end);
}

TEST_CASE("loader: the run ends exactly where fsk_scan_run ends it")
{
    struct Case
    {
        const char *name;
        Image img;
    };
    std::vector<Case> cases;

    {
        Image i = make_image(2, 300, 1);
        i.add_header("data", 2, 0);
        i.add_values({7});
        cases.push_back({"another chunk type", i});
    }
    {
        Image i = make_image(2, 300, 2);
        i.add_fsk(40, make_values(50, 9), false); // a fresh gap starts a new run
        cases.push_back({"fsk chunk with its own gap", i});
    }
    {
        Image i = make_image(2, 300, 3);
        i.bytes.push_back(0xAA); // fewer than 8 bytes after the last chunk
        i.bytes.push_back(0xBB);
        cases.push_back({"end of file", i});
    }
    {
        Image i = make_image(2, 300, 4);
        i.add_header("fsk ", 3, 0); // odd length
        i.bytes.insert(i.bytes.end(), {1, 2, 3});
        cases.push_back({"odd continuation", i});
    }
    {
        Image i = make_image(2, 300, 6);
        i.add_header("fsk ", 5000, 0); // payload past the end of the file
        i.bytes.insert(i.bytes.end(), {1, 2});
        cases.push_back({"overrunning continuation", i});
    }
    {
        Image i = make_image(FSK_RUN_MAX_CHUNKS + 3, 40, 8); // capped
        i.run_end = 0;
        // the run ends after FSK_RUN_MAX_CHUNKS chunks
        i.run_values.resize(FSK_RUN_MAX_CHUNKS * 40);
        size_t off = i.run_offset;
        for (size_t c = 0; c < FSK_RUN_MAX_CHUNKS; ++c)
            off += 8 + 80;
        i.run_end = off;
        cases.push_back({"chunk cap", i});
    }

    for (const Case &c : cases)
    {
        CAPTURE(c.name);
        Rig rig(c.img);
        bool ok = false;
        CHECK(rig.run_all(ok) == FSKStep::done);
        CHECK(ok);

        PlainFile scan_file = rig.file;
        scan_file.calls = 0;
        FSKRunInfo info;
        const bool scanned = fsk_scan_run(plain_read, &scan_file, c.img.bytes.size(), c.img.run_offset, info);
        if (scanned)
        {
            CHECK(rig.run.next_offset == info.next_offset);
            CHECK(rig.run.pub_values == info.value_count);
        }
        else
        {
            // fsk_scan_run rejects the whole run; the loader cannot take back what it has played, so
            // it ends the run before the bad chunk
            CHECK(rig.run.next_offset == c.img.run_end);
        }
        CHECK(rig.run.pub_values == c.img.run_values.size());
    }
}

TEST_CASE("loader: positive short reads are accumulated")
{
    const Image img = make_image(3, 900, 21);
    Rig rig(img);
    rig.file.max_read = 37;

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(ok);
    CHECK(rig.L.faults == 0);
    CHECK(rig.run.pub_values == img.run_values.size());
    CHECK(rig.run.next_offset == img.run_end);
}

// ------------------------------------------------------------------------------------------------
// Producer: faults, stop, allocation, capacity
// ------------------------------------------------------------------------------------------------

TEST_CASE("loader: a transient fault at any read is retried and the run is unchanged")
{
    const Image img = make_image(3, 700, 31);

    Rig clean(img);
    bool ok = false;
    CHECK(clean.run_all(ok) == FSKStep::done);
    const uint32_t total_calls = clean.file.calls;

    for (uint32_t k = 0; k < total_calls; ++k)
    {
        CAPTURE(k);
        Rig rig(img);
        rig.file.fail_call = [k](uint32_t call) { return call == k; };
        CHECK(rig.run_all(ok) == FSKStep::done);
        CHECK(ok); // no corrupt value was ever published
        CHECK(rig.L.faults == 1);
        CHECK(rig.run.pub_values == img.run_values.size());
        CHECK(rig.run.next_offset == img.run_end);
    }
}

TEST_CASE("loader: a persistent fault fails the run after the budget, published data stays valid")
{
    const Image img = make_image(3, 700, 41);
    Rig rig(img);
    // from the 5th read on, every read fails
    rig.file.fail_call = [](uint32_t call) { return call >= 4; };

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);
    CHECK(ok);
    CHECK(fsk_run_state(rig.run) == FSKLoaderState::failed);
    CHECK(rig.L.faults == FSKRunLoader::FAULT_BUDGET + 1);
    const uint32_t published = rig.run.pub_values;
    CHECK(published > 0);
    CHECK(published < img.run_values.size());
}

TEST_CASE("loader: recovery resumes from the exact confirmed cursor, nothing skipped or repeated")
{
    const Image img = make_image(3, 700, 51);
    Rig rig(img);
    bool healthy = false;
    // fail from the 6th read until recovery
    const uint32_t until = 5;
    rig.file.fail_call = [&healthy, &until](uint32_t call) { return !healthy && call >= until; };

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);
    const uint32_t before = rig.run.pub_values;
    const size_t pos_before = rig.L.file_pos;

    healthy = true;
    fsk_loader_recover(rig.L);
    CHECK(fsk_run_state(rig.run) == FSKLoaderState::loading);
    CHECK(rig.run.pub_values == before);
    CHECK(rig.L.file_pos == pos_before);
    CHECK(rig.L.fault_budget == FSKRunLoader::FAULT_BUDGET);

    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(ok);
    CHECK(rig.run.pub_values == img.run_values.size());
    CHECK(rig.run.next_offset == img.run_end);
    CHECK(rig.run.total_ticks == total_ticks(oracle_portions(img.run_values, SIZE_MAX)));
}

TEST_CASE("loader: an unreadable header that exists is a fault, never the end of the run")
{
    const Image img = make_image(3, 300, 61);
    struct Gate
    {
        PlainFile file;
        size_t header;
    } gate;
    gate.file.bytes = &img.bytes;
    gate.header = img.run_offset + 8 + 600; // the second chunk's header
    auto read = [](void *ctx, size_t off, uint8_t *dst, size_t n) -> size_t
    {
        Gate *g = static_cast<Gate *>(ctx);
        return off == g->header ? 0 : plain_read(&g->file, off, dst, n);
    };
    Rig rig(img, read, &gate);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);
    CHECK(fsk_run_state(rig.run) == FSKLoaderState::failed);
    CHECK(rig.run.pub_values == 300); // the first chunk, and no more
}

TEST_CASE("loader: stop is honoured at the next step, publishes nothing more and repeats")
{
    const Image img = make_image(3, 700, 71);
    Rig rig(img);
    for (int i = 0; i < 3; ++i)
        CHECK(fsk_loader_step(rig.L) == FSKStep::progress);
    const uint32_t before = rig.run.pub_values;

    fsk_pub_store(&rig.run.stop_req, 1);
    CHECK(fsk_loader_step(rig.L) == FSKStep::stopped);
    CHECK(fsk_loader_step(rig.L) == FSKStep::stopped);
    CHECK(fsk_run_state(rig.run) == FSKLoaderState::stopped);
    CHECK(rig.run.pub_values == before);
    CHECK(rig.prefix_ok());
}

TEST_CASE("loader: an allocation failure fails the run and recovery retries the same block")
{
    const Image img = make_image(2, 700, 81);
    Rig rig(img);
    rig.alloc.fail_at = 1; // the second block

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);
    CHECK(ok);
    CHECK(rig.run.pub_values == 256); // one whole block

    rig.alloc.fail_at = -1;
    fsk_loader_recover(rig.L);
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(ok);
    CHECK(rig.run.pub_values == img.run_values.size());
}

TEST_CASE("loader: a block table that is too small fails instead of writing past it")
{
    const Image img = make_image(2, 700, 91); // 2800 bytes: 6 blocks
    Rig rig(img, nullptr, nullptr, 3);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);
    CHECK(ok);
    CHECK(rig.run.pub_values <= 3 * FSK_LOADER_BLOCK_BYTES / 2);
    CHECK(rig.run.pub_values > 0);
}

TEST_CASE("loader: the progressive threshold and the block table size")
{
    CHECK_FALSE(fsk_progressive_wanted(FSK_PROGRESSIVE_MIN_FIRST_CHUNK_BYTES - 2));
    CHECK(fsk_progressive_wanted(FSK_PROGRESSIVE_MIN_FIRST_CHUNK_BYTES));
    CHECK(fsk_loader_table_blocks(504316, 8) == (504316 - 8 + 511) / 512 + 1);
    CHECK(fsk_loader_table_blocks(100, 500) == 1);
    CHECK(fsk_loader_table_blocks(SIZE_MAX / 2, 0) == FSK_LOADER_MAX_TABLE_BLOCKS);
}

// ------------------------------------------------------------------------------------------------
// Consumer: values and symbols
// ------------------------------------------------------------------------------------------------

TEST_CASE("consumer: values below the published count come back exact, the rest is underrun until final")
{
    const Image img = make_image(2, 700, 101);
    Rig rig(img);
    for (int i = 0; i < 4; ++i)
        fsk_loader_step(rig.L);
    const uint32_t pub = rig.run.pub_values;
    REQUIRE(pub > 0);

    const FSKPublishedValues src{&rig.run};
    uint16_t v = 0xFFFF;
    for (uint32_t i = 0; i < pub; ++i)
    {
        REQUIRE(src.next(i, v) == FSKNext::value);
        CHECK(v == img.run_values[i]);
    }
    CHECK(src.next(pub, v) == FSKNext::underrun);

    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(src.next(img.run_values.size() - 1, v) == FSKNext::value);
    CHECK(src.next(img.run_values.size(), v) == FSKNext::end);
}

TEST_CASE("consumer: a failed or stopped run ends in underrun, never in a clean end")
{
    const Image img = make_image(2, 700, 111);
    Rig rig(img);
    rig.file.fail_call = [](uint32_t call) { return call >= 3; };
    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::failed);

    const FSKPublishedValues src{&rig.run};
    uint16_t v = 0;
    CHECK(src.next(rig.run.pub_values, v) == FSKNext::underrun);
}

TEST_CASE("consumer: progressive output equals the preloaded waveform for any refill capacities")
{
    const Image img = make_image(5, 3000, 121);
    Rig rig(img);
    bool ok = false;
    REQUIRE(rig.run_all(ok) == FSKStep::done);

    std::vector<uint8_t> payload;
    for (uint16_t v : img.run_values)
    {
        payload.push_back(static_cast<uint8_t>(v & 0xFF));
        payload.push_back(static_cast<uint8_t>(v >> 8));
    }
    const FSKPreloadedValues preloaded{payload.data(), img.run_values.size()};
    const FSKPublishedValues published{&rig.run};
    const std::vector<Portion> oracle = oracle_portions(img.run_values, SIZE_MAX);

    for (uint32_t seed = 1; seed <= 200; ++seed)
    {
        FSKNext s1 = FSKNext::value, s2 = FSKNext::value;
        const std::vector<Portion> a = encode_all(preloaded, seed, s1);
        const std::vector<Portion> b = encode_all(published, seed, s2);
        REQUIRE(s1 == FSKNext::end);
        REQUIRE(s2 == FSKNext::end);
        CHECK(a == b);
        CHECK(b == oracle);
    }
    CHECK(total_ticks(oracle) == rig.run.total_ticks);
}

TEST_CASE("consumer: with the producer stalled, the waveform is exactly a prefix of the run and ends in underrun")
{
    const Image img = make_image(3, 1000, 131);
    Rig rig(img);
    for (int i = 0; i < 9; ++i)
        fsk_loader_step(rig.L);
    const uint32_t pub = rig.run.pub_values;
    REQUIRE(pub > 0);
    REQUIRE(pub < img.run_values.size());

    const FSKPublishedValues published{&rig.run};
    for (uint32_t seed = 1; seed <= 50; ++seed)
    {
        FSKNext stop = FSKNext::value;
        const std::vector<Portion> out = encode_all(published, seed, stop);
        CHECK(stop == FSKNext::underrun);
        CHECK(out == oracle_portions(img.run_values, pub)); // nothing unloaded, nothing invented
    }
}

TEST_CASE("consumer: a consumer slower than the producer never underruns")
{
    const Image img = make_image(4, 2000, 141);
    Rig rig(img);
    const FSKPublishedValues published{&rig.run};

    // before every refill of 256 symbols the producer is 1024 values ahead of the encoder
    FSKEncodeState st;
    std::vector<Portion> out;
    FSKNext stop = FSKNext::value;
    while (stop == FSKNext::value)
    {
        while (rig.run.pub_values < st.value_index + 1024 && fsk_loader_step(rig.L) == FSKStep::progress)
        {
        }
        std::vector<Sym> buf(256);
        const size_t n = fsk_fill_symbols(st, published, buf.data(), buf.size(), stop);
        for (size_t i = 0; i < n; ++i)
        {
            out.push_back({buf[i].duration0, buf[i].level0});
            if (buf[i].duration1 != 0)
                out.push_back({buf[i].duration1, buf[i].level1});
        }
    }
    CHECK(stop == FSKNext::end);
    CHECK(out == oracle_portions(img.run_values, SIZE_MAX));
}

TEST_CASE("consumer: a free-running producer and consumer never exchange unpublished data")
{
    const Image img = make_image(4, 4000, 151);
    Rig rig(img);
    const FSKPublishedValues published{&rig.run};

    std::thread producer([&rig] {
        FSKStep st;
        do
        {
            st = fsk_loader_step(rig.L);
            std::this_thread::yield();
        } while (st == FSKStep::progress);
    });

    // blocks are poisoned with 0xEE: a value read before it was published would not match
    size_t index = 0;
    bool mismatch = false;
    for (;;)
    {
        uint16_t v = 0;
        const FSKNext n = published.next(index, v);
        if (n == FSKNext::end)
            break;
        if (n == FSKNext::underrun)
        {
            std::this_thread::yield();
            continue; // only this test waits; the player stops
        }
        if (v != img.run_values[index])
            mismatch = true;
        ++index;
    }
    producer.join();
    CHECK_FALSE(mismatch);
    CHECK(index == img.run_values.size());
}

// ------------------------------------------------------------------------------------------------
// Start gate
// ------------------------------------------------------------------------------------------------

namespace
{

// Steps the producer until the gate opens; returns the published count at that moment.
uint32_t published_when_ready(Rig &rig, uint64_t min_runway_us)
{
    for (;;)
    {
        if (fsk_run_start_gate(rig.run, min_runway_us) == FSKStartGate::ready)
            return rig.run.pub_values;
        if (fsk_loader_step(rig.L) != FSKStep::progress)
            return fsk_run_start_gate(rig.run, min_runway_us) == FSKStartGate::ready ? rig.run.pub_values : 0;
    }
}

Image uniform_image(size_t values, uint16_t value)
{
    Image img;
    img.run_offset = 0;
    img.add_fsk(0, std::vector<uint16_t>(values, value));
    return img;
}

} // namespace

TEST_CASE("gate: the runway is measured in waveform time, not bytes")
{
    // 1 ms values: 20 s of tape is 20000 values, far more than the prefill
    const Image dense = uniform_image(30000, 10);
    Rig a(dense);
    const uint32_t at_runway = published_when_ready(a, FSK_RUNWAY_MIN_US);
    CHECK(at_runway >= 20000);
    CHECK(at_runway < 20000 + 256);

    // 100 ms values: 20 s is 200 values, so the prefill decides
    const Image sparse = uniform_image(30000, 1000);
    Rig b(sparse);
    const uint32_t at_prefill = published_when_ready(b, FSK_RUNWAY_MIN_US);
    CHECK(at_prefill >= FSK_PREFILL_VALUES);
    CHECK(at_prefill < FSK_PREFILL_VALUES + 256);
}

TEST_CASE("gate: the runway is 20 s and inclusive, the prefill is 1536 values")
{
    CHECK(FSK_RUNWAY_MIN_US == 20000000ULL);
    CHECK(FSK_PREFILL_VALUES == 1536);

    // 10 ms values; blocks publish 256 values at a time, so 2048 values = 20.48 s are the first to cover 20 s
    const Image img = uniform_image(4000, 100);
    Rig rig(img);
    while (rig.run.pub_values < 2000)
        fsk_loader_step(rig.L);
    REQUIRE(rig.run.pub_values == 2048);
    CHECK(fsk_run_start_gate(rig.run) == FSKStartGate::ready);
    CHECK(fsk_run_start_gate(rig.run, 20480000ULL) == FSKStartGate::ready);
    CHECK(fsk_run_start_gate(rig.run, 20480001ULL) == FSKStartGate::waiting);

    // 100 ms values cover 20 s with 200 values: only the prefill keeps the gate shut below 1536
    const Image sparse = uniform_image(4000, 1000);
    Rig sp(sparse);
    while (sp.run.pub_values < 1280)
        fsk_loader_step(sp.L);
    CHECK(fsk_run_start_gate(sp.run) == FSKStartGate::waiting);
    while (sp.run.pub_values < 1536)
        fsk_loader_step(sp.L);
    CHECK(fsk_run_start_gate(sp.run) == FSKStartGate::ready);
}

TEST_CASE("gate: a run shorter than the runway starts once it is final")
{
    const Image img = uniform_image(100, 50);
    Rig rig(img);
    CHECK(fsk_run_start_gate(rig.run) == FSKStartGate::waiting);
    bool ok = false;
    CHECK(rig.run_all(ok) == FSKStep::done);
    CHECK(fsk_run_start_gate(rig.run) == FSKStartGate::ready);
}

TEST_CASE("gate: a producer that died before the runway fails the gate, a live one only makes it wait")
{
    const Image img = make_image(3, 700, 161);

    Rig failing(img);
    failing.file.fail_call = [](uint32_t call) { return call >= 3; };
    bool ok = false;
    REQUIRE(failing.run_all(ok) == FSKStep::failed);
    CHECK(fsk_run_start_gate(failing.run) == FSKStartGate::failed);

    Rig stopped(img);
    fsk_loader_step(stopped.L);
    fsk_pub_store(&stopped.run.stop_req, 1);
    fsk_loader_step(stopped.L);
    CHECK(fsk_run_start_gate(stopped.run) == FSKStartGate::failed);

    Rig loading(img);
    fsk_loader_step(loading.L);
    CHECK(fsk_run_start_gate(loading.run) == FSKStartGate::waiting);
}
