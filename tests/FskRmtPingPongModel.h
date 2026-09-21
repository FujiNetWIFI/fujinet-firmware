#ifndef FSK_RMT_PING_PONG_MODEL_H
#define FSK_RMT_PING_PONG_MODEL_H

// Test-only host model of how ESP-IDF 5.4's non-DMA RMT TX driver drives the
// FujiNet FSK encoder callback (mem_block_symbols = 512, threshold = 256), and
// of the physical truth the hardware then makes available.
//
// The model reproduces the callback's structure (value -> portions of at most
// 32767 ticks -> two portions per symbol, zero-duration values skipped, done on
// the last symbol) and drives the SAME pure bookkeeping helpers the production
// callback uses (fsk_bounds_*, fsk_ref_log_*). The oracle is independent: it
// splits values into portions with its own arithmetic and sums them directly,
// so it does not share the tracker's logic.
//
// Driver sequence being modelled (see the FskBoundaryTracker comment):
//   prefill      callback offered 512 symbols, once, inside rmt_transmit()
//   threshold n  the driver calls the callback offered 256 symbols (unless
//                already done) when the transmitter has FETCHED the whole first
//                half: at that instant the pin is at the start of the second
//                entry of the second-to-last symbol, i.e. still kHwLeadEntries
//                entries (b1, a0, b0) short of the half's boundary (measured on
//                the FujiNet over 495 thresholds, 1 us standard deviation; see
//                the FskBoundaryTracker comment).

#include "sio/fsk_plan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fskmodel
{
    // The hardware behavior being modelled: entries (half symbol words) of the
    // resident half still to be played when its threshold event fires.
    // Deliberately independent of the production constant
    // FSK_RMT_PREFETCH_ENTRIES, so a wrong production value shows up as a failing
    // test instead of agreeing with itself.
    constexpr size_t kHwLeadEntries = 3;

    // Independent oracle: the flat list of RMT portions a value sequence
    // produces. `first_skip_ticks` is removed from the first non-zero value
    // (a run resumed inside a value).
    inline std::vector<uint32_t> oracle_portions(const std::vector<uint16_t> &values,
                                                 uint64_t first_skip_ticks = 0)
    {
        std::vector<uint32_t> out;
        bool first = true;
        for (uint16_t v : values)
        {
            if (v == 0)
                continue;
            uint64_t rem = static_cast<uint64_t>(v) * 100ULL; // 1 A8CAS unit = 100 us
            if (first)
            {
                rem -= first_skip_ticks;
                first = false;
            }
            while (rem > 0)
            {
                const uint64_t p = rem > 32767ULL ? 32767ULL : rem;
                out.push_back(static_cast<uint32_t>(p));
                rem -= p;
            }
        }
        return out;
    }

    // Waveform ticks (seed included) of the first `symbols` symbols: each symbol
    // is two portions (the last one may hold a single portion).
    inline uint64_t oracle_ticks_of_symbols(const std::vector<uint32_t> &portions,
                                            uint64_t seed_ticks, size_t symbols)
    {
        uint64_t t = seed_ticks;
        const size_t n = symbols * 2 < portions.size() ? symbols * 2 : portions.size();
        for (size_t i = 0; i < n; ++i)
            t += portions[i];
        return t;
    }

    inline size_t oracle_symbol_count(const std::vector<uint32_t> &portions)
    {
        return (portions.size() + 1) / 2;
    }

    class RmtPingPongModel
    {
    public:
        RmtPingPongModel(std::vector<uint16_t> values, uint64_t seed_ticks,
                         uint64_t first_skip_ticks = 0, int64_t seed_ts_us = 0)
            : values_(std::move(values)), skip_(first_skip_ticks)
        {
            fsk_bounds_reset(bt, seed_ticks);
            fsk_ref_log_reset(log, seed_ticks);
            fsk_ref_log_set_seed_ts(log, seed_ts_us);
        }

        FskBoundaryTracker bt{};
        FskRefLog          log{};
        bool               done = false;
        size_t             calls = 0;

        // One encoder-callback invocation offering `offered` symbols; mirrors
        // sioCassette::fsk_encode_cb. Returns *done.
        bool callback(size_t offered, int64_t now_us)
        {
            const bool is_prefill = (calls++ == 0);
            fsk_bounds_refill_begin(bt, is_prefill, log, is_prefill ? 0 : now_us);

            size_t num = 0;
            while (num < offered)
            {
                int half = 0;
                while (half < 2)
                {
                    if (remaining_ == 0)
                    {
                        bool got = false;
                        while (idx_ < values_.size())
                        {
                            const uint16_t v = values_[idx_++];
                            if (v != 0)
                            {
                                remaining_ = fsk_ticks_for_value(v);
                                if (first_)
                                {
                                    remaining_ -= static_cast<uint32_t>(skip_);
                                    first_ = false;
                                }
                                got = true;
                                break;
                            }
                        }
                        if (!got)
                        {
                            if (half == 0)
                            {
                                done = true;
                                return true; // clean symbol boundary: complete
                            }
                            half++; // pad the unused second half (0 duration)
                            break;
                        }
                    }
                    const uint32_t portion = fsk_next_portion(remaining_);
                    remaining_ -= portion;
                    fsk_bounds_add_portion(bt, portion);
                    half++;
                }
                num++;
                fsk_bounds_symbol_written(bt, is_prefill, num);
                if (remaining_ == 0 && idx_ >= values_.size())
                {
                    done = true;
                    return true;
                }
            }
            fsk_bounds_refill_full(bt, is_prefill);
            return false;
        }

    private:
        std::vector<uint16_t> values_;
        uint64_t skip_;
        bool     first_ = true;
        size_t   idx_ = 0;
        uint32_t remaining_ = 0;
    };

    // What one threshold callback recorded, next to the physical truth.
    struct ThresholdRecord
    {
        size_t   n;                 // threshold number, 1-based
        int64_t  ts_us;             // when the callback ran (threshold event + ISR latency)
        uint64_t confirmed;         // reference the callback recorded
        uint64_t expected;          // oracle: RAW boundary, ticks of symbols 0 .. 256*n-1 (seed included)
        uint64_t expected_ref;      // oracle: the boundary minus the last FSK_RMT_PREFETCH_ENTRIES entries
        uint64_t hw_position;       // where the pin really is when the threshold event fires
        uint64_t cumulative_before; // encoded total when the refill began
        uint64_t cumulative_after;  // encoded total when the refill ended
        uint64_t next_pending;      // boundary the following threshold will prove
        bool     done_after;        // the callback finished the waveform
    };

    struct Simulation
    {
        std::vector<uint32_t>        portions;
        std::vector<ThresholdRecord> thresholds;
        uint64_t prefill_pending = 0;    // pending boundary after the prefill
        uint64_t prefill_cumulative = 0; // encoded total after the prefill
        size_t   prefill_refs = 0;       // references recorded by the prefill (must be 0)
        uint64_t total_ticks = 0;        // full encoded total (seed included)
        bool     completed = false;
    };

    // Prefix ticks of the ENTRIES (portions) of an oracle portion list, seed
    // included: entry_end[j] = seed + ticks of the first j entries. Symbol k ends at
    // entry_end[2k] (its two entries are 2k-2 and 2k-1).
    inline std::vector<uint64_t> oracle_entry_ends(const std::vector<uint32_t> &portions,
                                                   uint64_t seed_ticks)
    {
        std::vector<uint64_t> e(1, seed_ticks);
        for (uint32_t p : portions)
            e.push_back(e.back() + p);
        return e;
    }

    // Runs prefill and up to `max_thresholds` threshold callbacks. The threshold
    // event of half n fires when the pin is `hw_lead` entries short of that
    // half's boundary (the boundary minus the durations of its last `hw_lead`
    // entries); the callback runs `isr_latency_us` after the event.
    inline Simulation simulate(RmtPingPongModel &m, const std::vector<uint16_t> &values,
                               uint64_t seed_ticks, uint64_t first_skip_ticks,
                               int64_t t_start_us, int64_t isr_latency_us,
                               size_t max_thresholds, size_t hw_lead = kHwLeadEntries)
    {
        Simulation s;
        s.portions = oracle_portions(values, first_skip_ticks);
        const std::vector<uint64_t> entry_end = oracle_entry_ends(s.portions, seed_ticks);
        const size_t nent = entry_end.size() - 1;
        const size_t nsym = (nent + 1) / 2;

        m.callback(2 * FSK_RMT_HALF_SYMBOLS, t_start_us);
        s.prefill_pending = m.bt.pending_ticks;
        s.prefill_cumulative = m.bt.cumulative_ticks;
        s.prefill_refs = m.log.count;

        for (size_t n = 1; n <= max_thresholds && !m.done; ++n)
        {
            ThresholdRecord r{};
            r.n = n;
            const size_t k = std::min<size_t>(FSK_RMT_HALF_SYMBOLS * n, nsym);
            const size_t j = std::min<size_t>(2 * k, nent);   // entries up to the half's boundary
            r.expected = entry_end[j];
            r.expected_ref = entry_end[j >= FSK_RMT_PREFETCH_ENTRIES ? j - FSK_RMT_PREFETCH_ENTRIES : 0];
            r.hw_position = entry_end[j >= hw_lead ? j - hw_lead : 0];
            r.ts_us = t_start_us + static_cast<int64_t>(r.hw_position - seed_ticks) + isr_latency_us;
            r.cumulative_before = m.bt.cumulative_ticks;
            const bool done = m.callback(FSK_RMT_HALF_SYMBOLS, r.ts_us);
            r.cumulative_after = m.bt.cumulative_ticks;
            r.next_pending = m.bt.pending_ticks;
            r.done_after = done;
            r.confirmed = m.log.ring[(m.log.count - 1u) % FSK_REF_RING].ticks;
            s.thresholds.push_back(r);
        }
        s.total_ticks = m.bt.cumulative_ticks;
        s.completed = m.done;
        return s;
    }
} // namespace fskmodel

#endif // FSK_RMT_PING_PONG_MODEL_H
