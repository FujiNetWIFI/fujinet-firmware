#ifndef FSK_TEST_IMAGES_H
#define FSK_TEST_IMAGES_H

// Synthetic A8CAS images, a positional fake file, a block allocator and an independent encoder oracle
// for the FSK tests. Deterministic: no corpus files.

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "media/atari/casFSK.h"
#include "media/atari/casFSKLoader.h"

namespace fsktest
{

struct Rng
{
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next()
    {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    uint32_t below(uint32_t n) { return next() % n; }
};

// Mostly short values, a few zeros (they still take a parity slot) and a few long ones that split
// into several RMT portions.
inline std::vector<uint16_t> make_values(size_t count, uint32_t seed)
{
    Rng r(seed);
    std::vector<uint16_t> v(count);
    for (auto &x : v)
    {
        const uint32_t k = r.below(100);
        x = k < 3 ? 0 : k < 6 ? static_cast<uint16_t>(400 + r.below(2000)) : static_cast<uint16_t>(1 + r.below(200));
    }
    return v;
}

struct Image
{
    std::vector<uint8_t> bytes;
    size_t run_offset = 0;
    size_t run_end = 0;
    std::vector<uint16_t> run_values; // every value of the run, in order

    void add_header(const char type[4], uint16_t length, uint16_t irg)
    {
        const size_t base = bytes.size();
        bytes.resize(base + 8);
        std::memcpy(&bytes[base], type, 4);
        bytes[base + 4] = static_cast<uint8_t>(length & 0xFF);
        bytes[base + 5] = static_cast<uint8_t>(length >> 8);
        bytes[base + 6] = static_cast<uint8_t>(irg & 0xFF);
        bytes[base + 7] = static_cast<uint8_t>(irg >> 8);
    }

    void add_values(const std::vector<uint16_t> &values)
    {
        for (uint16_t v : values)
        {
            bytes.push_back(static_cast<uint8_t>(v & 0xFF));
            bytes.push_back(static_cast<uint8_t>(v >> 8));
        }
    }

    // An fsk chunk. `in_run` adds its values to the run oracle.
    void add_fsk(uint16_t irg, const std::vector<uint16_t> &values, bool in_run = true)
    {
        add_header("fsk ", static_cast<uint16_t>(values.size() * 2), irg);
        add_values(values);
        if (in_run)
        {
            run_values.insert(run_values.end(), values.begin(), values.end());
            run_end = bytes.size();
        }
    }

    size_t first_length() const { return fsk_decode_le16(&bytes[run_offset + 4]); }
};

// A run of `chunks` chunks, `values_per_chunk` values each, after a FUJI header. Chunk 0 carries the
// gap, the rest join it (irg 0).
inline Image make_image(size_t chunks, size_t values_per_chunk, uint32_t seed)
{
    Image img;
    img.add_header("FUJI", 0, 0);
    img.run_offset = img.bytes.size();
    for (size_t c = 0; c < chunks; ++c)
        img.add_fsk(c == 0 ? 999 : 0, make_values(values_per_chunk, seed + static_cast<uint32_t>(c)));
    return img;
}

// ---- fake file ------------------------------------------------------------------------------

struct PlainFile
{
    const std::vector<uint8_t> *bytes = nullptr;
    size_t max_read = SIZE_MAX;                  // cap per call: short reads
    std::function<bool(uint32_t)> fail_call;     // true: this call returns 0
    uint32_t calls = 0;
    size_t high_water = 0;                       // end of the furthest byte returned
};

inline size_t plain_read(void *ctx, size_t off, uint8_t *dst, size_t n)
{
    PlainFile *f = static_cast<PlainFile *>(ctx);
    const uint32_t call = f->calls++;
    if (f->fail_call && f->fail_call(call))
        return 0;
    if (off >= f->bytes->size())
        return 0;
    size_t take = n < f->bytes->size() - off ? n : f->bytes->size() - off;
    take = take < f->max_read ? take : f->max_read;
    std::memcpy(dst, f->bytes->data() + off, take);
    if (off + take > f->high_water)
        f->high_water = off + take;
    return take;
}

// ---- block allocator -------------------------------------------------------------------------

struct Alloc
{
    std::vector<std::unique_ptr<uint8_t[]>> keep;
    int fail_at = -1; // the allocation with this index fails
    int count = 0;
};

inline uint8_t *alloc_block(void *ctx, size_t n)
{
    Alloc *a = static_cast<Alloc *>(ctx);
    if (a->count++ == a->fail_at)
        return nullptr;
    a->keep.emplace_back(new uint8_t[n]);
    std::memset(a->keep.back().get(), 0xEE, n); // poison: reading an unloaded block is visible
    return a->keep.back().get();
}

// ---- producer rig ----------------------------------------------------------------------------

struct Rig
{
    const Image &img;
    PlainFile file;
    Alloc alloc;
    std::vector<uint8_t *> table;
    FSKProgressiveRun run;
    FSKRunLoader L;

    explicit Rig(const Image &image, FSKReadFn read = plain_read, void *read_ctx = nullptr,
                 size_t table_blocks = 0)
        : img(image)
    {
        file.bytes = &img.bytes;
        if (table_blocks == 0)
            table_blocks = fsk_loader_table_blocks(img.bytes.size(), img.run_offset);
        table.assign(table_blocks, nullptr);
        fsk_run_bind(run, table.data(), table.size());
        fsk_loader_init(L, run, read ? read : plain_read, read_ctx ? read_ctx : &file, &alloc_block,
                        &alloc, img.bytes.size(), img.run_offset,
                        static_cast<uint16_t>(img.first_length()));
    }

    // Every published value equals the oracle's value at that index.
    bool prefix_ok() const
    {
        const uint32_t pub = fsk_pub_load(&run.pub_values);
        if (pub > img.run_values.size())
            return false;
        for (uint32_t i = 0; i < pub; ++i)
            if (fsk_run_value(run, i) != img.run_values[i])
                return false;
        return true;
    }

    // Steps until the producer stops making progress; checks the published prefix after every step.
    FSKStep run_all(bool &always_ok, size_t max_steps = 10000000)
    {
        always_ok = true;
        uint32_t last_pub = 0;
        for (size_t i = 0; i < max_steps; ++i)
        {
            const FSKStep st = fsk_loader_step(L);
            const uint32_t pub = fsk_pub_load(&run.pub_values);
            always_ok = always_ok && pub >= last_pub && prefix_ok();
            last_pub = pub;
            if (st != FSKStep::progress)
                return st;
        }
        return FSKStep::progress;
    }
};

// ---- encoder oracle --------------------------------------------------------------------------

struct Sym
{
    uint16_t duration0;
    bool level0;
    uint16_t duration1;
    bool level1;
};

struct Portion
{
    uint32_t ticks;
    bool level;
    bool operator==(const Portion &o) const { return ticks == o.ticks && level == o.level; }
};

// What an RMT should be fed for `values[0..count)`, computed independently of casFSK.h.
inline std::vector<Portion> oracle_portions(const std::vector<uint16_t> &values, size_t count)
{
    std::vector<Portion> out;
    for (size_t i = 0; i < count && i < values.size(); ++i)
    {
        uint32_t ticks = static_cast<uint32_t>(values[i]) * 100u;
        while (ticks > 0)
        {
            const uint32_t p = ticks > 32767u ? 32767u : ticks;
            out.push_back({p, (i & 1u) != 0});
            ticks -= p;
        }
    }
    return out;
}

// Encodes with a random sequence of symbol capacities, as the RMT driver would offer them. `stop`
// is why it ended.
template <class Source>
std::vector<Portion> encode_all(const Source &source, uint32_t seed, FSKNext &stop)
{
    Rng rng(seed);
    FSKEncodeState st;
    std::vector<Portion> out;
    for (;;)
    {
        const size_t cap = 1 + rng.below(600);
        std::vector<Sym> buf(cap);
        const size_t n = fsk_fill_symbols(st, source, buf.data(), cap, stop);
        for (size_t i = 0; i < n; ++i)
        {
            out.push_back({buf[i].duration0, buf[i].level0});
            if (buf[i].duration1 != 0)
                out.push_back({buf[i].duration1, buf[i].level1});
        }
        if (stop != FSKNext::value)
            return out;
    }
}

inline uint64_t total_ticks(const std::vector<Portion> &p)
{
    uint64_t t = 0;
    for (const Portion &x : p)
        t += x.ticks;
    return t;
}

} // namespace fsktest

#endif // FSK_TEST_IMAGES_H
