#ifndef FSK_TRANSPORT_MODEL_H
#define FSK_TRANSPORT_MODEL_H

// Test-only discrete-time model of one raw-FSK run being paused and resumed by
// MOTOR: freeze position, hardware abort (or the old drain), teardown, resident
// run (or a new SD preload) and the restart of a new transmission from the frozen
// position. It drives the production pure helpers (fsk_decide_emit, the reference
// log, cas_fsk_locate_ticks) and the ping-pong bookkeeping model, and charges the
// steps that are not pure logic with costs MEASURED on the FujiNet:
//   * SD preload  5.64 s for 504,316 B  -> 11 us per byte, plus ~30 ms per header read
//   * channel create + RMT prefill (IRG_DONE -> TX_START)  1.4 ms
// so the model reproduces the reported behavior of v4 (a MOTOR cycle costs the
// drain of the queued symbols, then a preload) and shows what the fix changes.
//
// What it does NOT prove: that the ESP32 hardware really ends the transmission
// within the symbol in flight after the abort. That is the documented behavior of
// the legacy ESP32 rmt_tx_stop() sequence and is checked on the real device.

#include "sio/cassette_time_plan.h"
#include "sio/fsk_plan.h"
#include "FskRmtPingPongModel.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace fsktransport
{
    struct Cfg
    {
        int64_t task_latency_us = 300;     // MOTOR edge -> the emit task takes its snapshot
        int64_t abort_to_done_syms = 2;    // symbols the hardware still finishes after the abort
        int64_t svc_latency_us = 1000;     // fsk_background() sees hw_done and tears the channel down
        int64_t enable_latency_us = 500;   // service loop: MOTOR ON -> cassette enabled -> dispatch
        int64_t begin_latency_us = 1400;   // fsk_signal_begin() + rmt_transmit() prefill (measured)
        int64_t preload_us_per_byte = 11;  // SD preload (measured 5.64 s / 504,316 B)
        int64_t scan_us = 30000;           // run header scan
        int64_t isr_latency_us = 50;
        size_t  hw_lead_entries = fskmodel::kHwLeadEntries; // pin trails a half's boundary by this many entries at its threshold
        bool    fast_abort = true;         // fix: abort the RMT in hardware at freeze
        bool    keep_resident = true;      // fix: keep the preloaded run across the freeze
    };

    struct Cycle
    {
        int64_t  motor_on_us = 0;
        int64_t  tx_start_us = 0;          // 0 if the waveform never started (MOTOR OFF earlier)
        int64_t  motor_off_us = 0;
        int64_t  old_pin_end_us = 0;       // last instant the old transmission could drive DATA IN
        int64_t  old_hw_done_us = 0;       // hardware finished the old transaction
        int64_t  channel_free_us = 0;      // channel + encoder destroyed, service may restart
        int64_t  resume_latency_us = 0;    // MOTOR ON -> waveform start
        int64_t  preload_us = 0;
        uint64_t seed_ticks = 0;
        uint64_t frozen_ticks = 0;
        uint64_t true_ticks = 0;           // where the tape really was at the MOTOR OFF
        bool     preloaded = false;
        bool     started = false;
        bool     froze = false;
        bool     natural_end = false;
    };

    class Transport
    {
    public:
        Transport(std::vector<uint16_t> values, size_t run_bytes, Cfg cfg)
            : vals_(std::move(values)), run_bytes_(run_bytes), cfg_(cfg) {}

        uint64_t position() const { return pos_; }
        bool resident() const { return resident_; }
        int preloads() const { return preloads_; }
        int64_t channel_free_us() const { return idle_; }
        uint64_t total_ticks() const
        {
            uint64_t t = 0;
            for (uint16_t v : vals_)
                t += fsk_ticks_for_value(v);
            return t;
        }

        // Set the position from outside (rewind): the frozen run is abandoned.
        void set_position(uint64_t ticks)
        {
            pos_ = ticks;
            resident_ = false;
        }

        // HTTP rewind while the previous transaction may still be draining: the
        // cached run is invalidated at once; a draining transaction's cleanup then
        // frees the blocks instead of keeping them.
        void http_rewind(uint64_t ticks)
        {
            set_position(ticks);
        }

        Cycle run(int64_t motor_on_us, int64_t motor_off_us)
        {
            Cycle c;
            c.motor_on_us = motor_on_us;
            c.motor_off_us = motor_off_us;
            c.seed_ticks = pos_;

            // The service loop cannot restart the cassette while the old
            // transaction still owns the channel and blocks.
            const int64_t t_enable = std::max(motor_on_us, idle_) + cfg_.enable_latency_us;

            int64_t cost = 0;
            if (!resident_)
            {
                cost = cfg_.scan_us + static_cast<int64_t>(run_bytes_) * cfg_.preload_us_per_byte;
                c.preloaded = true;
                c.preload_us = cost;
                ++preloads_;
                resident_ = true; // loaded now
            }
            const int64_t t_start = t_enable + cost + cfg_.begin_latency_us;

            if (motor_off_us <= t_start)
            {
                // MOTOR OFF during preload / before the waveform starts: the position is unchanged.
                c.froze = true;
                c.frozen_ticks = pos_;
                c.true_ticks = pos_;
                c.channel_free_us = std::max(motor_off_us, t_enable) + cfg_.svc_latency_us;
                idle_ = c.channel_free_us;
                resident_ = cfg_.keep_resident;
                return c;
            }
            c.started = true;
            c.tx_start_us = t_start;
            c.resume_latency_us = t_start - motor_on_us;

            // Where the run resumes: the production locate over the resident run.
            const size_t count = vals_.size();
            FskLocateResult loc = cas_fsk_locate_ticks(
                &count, 1,
                [](void *ctx, size_t, size_t v) -> uint16_t
                { return (*static_cast<std::vector<uint16_t> *>(ctx))[v]; },
                &vals_, pos_);
            if (loc.at_end)
            {
                c.natural_end = true;
                c.frozen_ticks = pos_;
                c.true_ticks = pos_;
                idle_ = t_start + cfg_.svc_latency_us;
                c.channel_free_us = idle_;
                resident_ = false;
                return c;
            }
            const std::vector<uint16_t> slice(vals_.begin() + static_cast<long>(loc.value_index),
                                              vals_.end());
            const std::vector<uint32_t> portions = fskmodel::oracle_portions(slice, loc.skip_ticks);
            const std::vector<uint64_t> entry_end = fskmodel::oracle_entry_ends(portions, pos_);
            std::vector<uint64_t> sym_end(1, pos_);
            for (size_t i = 0; i < portions.size(); i += 2)
                sym_end.push_back(sym_end.back() + portions[i] +
                                  (i + 1 < portions.size() ? portions[i + 1] : 0));
            const uint64_t total = sym_end.back();

            c.true_ticks = std::min<uint64_t>(pos_ + static_cast<uint64_t>(motor_off_us - t_start), total);

            // Refill callbacks that ran before the emit task took its snapshot.
            const int64_t t_snap = motor_off_us + cfg_.task_latency_us;
            size_t n_thr = 0;
            for (size_t n = 1;; ++n)
            {
                const size_t k = std::min<size_t>(256 * n, sym_end.size() - 1);
                const size_t j = std::min<size_t>(2 * k, entry_end.size() - 1);
                const size_t jh = j >= cfg_.hw_lead_entries ? j - cfg_.hw_lead_entries : 0;
                const int64_t ts = t_start + static_cast<int64_t>(entry_end[jh] - pos_) + cfg_.isr_latency_us;
                if (ts > t_snap || k == sym_end.size() - 1)
                    break;
                n_thr = n;
            }
            fskmodel::RmtPingPongModel m(slice, pos_, loc.skip_ticks, t_start);
            const fskmodel::Simulation sim =
                fskmodel::simulate(m, slice, pos_, loc.skip_ticks, t_start, cfg_.isr_latency_us, n_thr,
                                   cfg_.hw_lead_entries);
            (void)sim;

            const FskEmitDecision d =
                fsk_decide_emit(false, FskStopReason::MOTOR, m.done, &m.log,
                                m.bt.cumulative_ticks, motor_off_us);
            if (d.outcome == FskEmitOutcome::NATURAL)
            {
                c.natural_end = true;
                c.frozen_ticks = total;
                pos_ = total;
                idle_ = t_start + static_cast<int64_t>(total - c.seed_ticks) + cfg_.svc_latency_us;
                c.channel_free_us = idle_;
                resident_ = false;
                return c;
            }

            c.froze = true;
            c.frozen_ticks = d.physical_ticks;
            pos_ = d.physical_ticks;

            const int64_t t_abort = t_snap + 50;
            c.old_pin_end_us = t_abort; // the pin is muted from here on
            const uint64_t played_at_abort =
                std::min<uint64_t>(c.seed_ticks + static_cast<uint64_t>(t_abort - t_start), total);
            uint64_t hw_done_ticks;
            if (cfg_.fast_abort)
            {
                const size_t k_in = static_cast<size_t>(
                    std::upper_bound(sym_end.begin(), sym_end.end(), played_at_abort) - sym_end.begin());
                const size_t k_done = std::min<size_t>(k_in + static_cast<size_t>(cfg_.abort_to_done_syms) - 1,
                                                       sym_end.size() - 1);
                hw_done_ticks = sym_end[k_done];
            }
            else
            {
                hw_done_ticks = m.bt.cumulative_ticks; // plays out everything already queued
            }
            c.old_hw_done_us = t_start + static_cast<int64_t>(hw_done_ticks - c.seed_ticks);
            c.channel_free_us = std::max(c.old_hw_done_us, t_abort) + cfg_.svc_latency_us;
            idle_ = c.channel_free_us;
            resident_ = cfg_.keep_resident;
            return c;
        }

    private:
        std::vector<uint16_t> vals_;
        size_t run_bytes_;
        Cfg cfg_;
        uint64_t pos_ = 0;
        bool resident_ = false;
        int preloads_ = 0;
        int64_t idle_ = 0;
    };
} // namespace fsktransport

#endif // FSK_TRANSPORT_MODEL_H
