#ifndef CASSETTE_FSK_H
#define CASSETTE_FSK_H

// cassetteFsk.h — device-side playback of one A8CAS `fsk ` run: preload into
// PSRAM, play once via RMT to completion, free, return. Basic playback only:
// no progressive loader, no background task, no runway/watermark, no
// resilient remote reader, no resident store kept across calls, no
// FSK-specific rewind state, no MOTOR freeze/resume timebase. On any
// failure, or a MOTOR drop mid-run, the caller replays the whole run from
// the same file offset on its next call — there is nothing to resume.
//
// Chunk/run interpretation itself lives in ../../media/atari/casFsk.h; this
// module owns only the hardware and the file bytes.

#include <cstddef>
#include <cstdint>

#include "fnio.h"

enum class CassetteFskStatus : uint8_t
{
    ok,                 // the run played to completion; next_offset/waveform_ms are valid
    malformed,          // header_offset was not a well-formed `fsk ` run (see casFsk.h::fsk_scan_run)
    allocation_failed,  // the PSRAM preload buffer, or an RMT resource, could not be obtained
    read_failed,        // a header or payload read came up short while preloading
    transmit_failed,    // rmt_transmit() itself did not accept the run
    motor_dropped,      // the MOTOR line dropped while the run was transmitting
};

struct CassetteFskPlayResult
{
    CassetteFskStatus status      = CassetteFskStatus::malformed;
    size_t            next_offset = 0; // valid only when status == ok
    uint32_t          waveform_ms = 0; // duration of the waveform itself, excluding the leading IRG gap
};

// Polled roughly every 100 ms while a run's RMT transmission is in flight.
// Returns true once the caller considers the MOTOR line genuinely dropped
// (e.g. pulldown mode and the line has been low). `ctx` is caller-defined.
using CassetteFskMotorDroppedFn = bool (*)(void *ctx);

#ifdef ESP_PLATFORM

// Preloads the zero-IRG-joined `fsk ` run starting at `header_offset` (a
// file offset already known to be an `fsk ` chunk header) into one PSRAM
// buffer, plays it via one RMT transmission to completion, frees the
// buffer, and returns. `header_offset` is read again from `file` (the scan
// in casFsk.h::fsk_scan_run reads headers only, not payload), so the caller
// need not have anything pre-read into a buffer of its own.
CassetteFskPlayResult cassette_fsk_play_run(fnFile *file, size_t filesize, size_t header_offset,
                                             CassetteFskMotorDroppedFn motor_dropped, void *motor_ctx);

#endif // ESP_PLATFORM

#endif // CASSETTE_FSK_H
