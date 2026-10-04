#ifndef CASSETTE_FSK_H
#define CASSETTE_FSK_H

// cassetteFSK.h — device-side playback of one A8CAS `fsk ` run: load it, play it once via RMT to
// completion, free it, return. A run whose first chunk is large is loaded progressively while it plays
// (cassetteFSKLoader.h); a smaller one is preloaded whole into PSRAM. Nothing is kept across calls:
// on any failure, or a MOTOR drop mid-run, the caller replays the whole run from the same file offset
// on its next call — there is nothing to resume.
//
// Chunk/run interpretation itself lives in ../../media/atari/casFSK.h; this
// module owns only the hardware and the file bytes.

#include <cstddef>
#include <cstdint>

#include "fnio.h"

enum class CassetteFSKStatus : uint8_t
{
    ok,                 // the run played to completion; next_offset/waveform_ms are valid
    malformed,          // header_offset was not a well-formed `fsk ` run (see casFSK.h::fsk_scan_run)
    allocation_failed,  // the PSRAM preload buffer, or an RMT resource, could not be obtained
    read_failed,        // a header or payload read came up short while preloading
    transmit_failed,    // rmt_transmit() itself did not accept the run
    motor_dropped,      // the MOTOR line dropped while the run was starting or transmitting
    underrun,           // the encoder reached values the loader had not published; the run was cut short
    stopped,            // the loader was stopped (unmount) before the run could finish
};

struct CassetteFSKPlayResult
{
    CassetteFSKStatus status      = CassetteFSKStatus::malformed;
    size_t            next_offset = 0; // valid only when status == ok
    uint32_t          waveform_ms = 0; // duration of the waveform itself, excluding the leading IRG gap
};

// Polled roughly every 100 ms while a run is waiting for its runway or its RMT transmission is in
// flight. Returns true once the caller considers the MOTOR line genuinely dropped
// (e.g. pulldown mode and the line has been low). `ctx` is caller-defined.
using CassetteFSKMotorDroppedFn = bool (*)(void *ctx);

// Declared, and implemented, only for the Atari build: the implementation is
// SIO-bus-specific (SYSTEM_BUS.flushOutput(), the SIO DATA IN pin), and only
// cassette.cpp (itself entirely #ifdef BUILD_ATARI) ever calls it. Other
// ESP_PLATFORM targets (e.g. ADAM) must not see this declaration without a
// matching definition, or a future caller elsewhere would hit a link error
// instead of a clear compile-time scope error.
#if defined(ESP_PLATFORM) && defined(BUILD_ATARI)

#include "cassetteFSKLoader.h"

// Plays the zero-IRG-joined `fsk ` run starting at `header_offset` (a file offset already known to be
// an `fsk ` chunk header whose payload is `chunk_length` bytes) via one RMT transmission to
// completion. A run with a large first chunk is streamed by `loader`, which is stopped and released
// before this returns; if the loader cannot be started, or fails before the waveform could start,
// the run is preloaded whole instead.
CassetteFSKPlayResult cassette_fsk_play_run(fnFile *file, size_t filesize, size_t header_offset,
                                             uint16_t chunk_length,
                                             CassetteFSKMotorDroppedFn motor_dropped, void *motor_ctx,
                                             CassetteFSKLoader &loader);

#endif // ESP_PLATFORM && BUILD_ATARI

#endif // CASSETTE_FSK_H
