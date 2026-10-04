#ifndef CASSETTE_FSK_READ_H
#define CASSETTE_FSK_READ_H

// cassetteFSKRead.h — reading a file that can lose its position. A remote READ carries no offset, so a
// lost reply leaves the server ahead of the client, and stdio answers an fseek() to where it believes it
// is without touching the file: the next read returns shifted bytes with no error. So a short read before
// the end of the file is a fault; after one the position is re-established with a real seek (away and
// back) and the whole request is read again, its first bytes compared with what the faulty attempt gave.
// Pure: the file is reached through injected operations.

#include <cstddef>
#include <cstdint>

struct FSKFileOps
{
    void  *ctx;
    long   (*tell)(void *ctx);                         // the stream's own idea of its position
    int    (*seek)(void *ctx, size_t off);             // absolute; 0 on success, like fseek
    size_t (*read)(void *ctx, uint8_t *dst, size_t n); // may return less than n
    bool   (*stopping)(void *ctx);                     // true: stop retrying
};

struct FSKReadStats
{
    uint32_t faults = 0;     // reads that came back short before the end of the file, or failed to seek
    uint32_t resyncs = 0;    // real absolute re-seeks performed
    uint32_t mismatches = 0; // re-reads whose overlap disagreed with what the faulty attempt delivered
};

constexpr unsigned FSK_READ_ATTEMPTS = 3;

// How much of a faulty attempt's data is kept to compare against the re-read.
constexpr size_t FSK_READ_OVERLAP_BYTES = 64;

// Reads exactly `n` bytes at `off`, or returns 0 (nothing usable). Returns fewer than `n` only at the
// true end of the file (off + result >= filesize). `suspect` is the caller's persistent "stream
// position unknown" flag: set by a fault (or by the caller after a restart), cleared by a read that
// completed.
size_t fsk_resilient_read(const FSKFileOps &f, size_t filesize, bool &suspect, FSKReadStats &st,
                          size_t off, uint8_t *dst, size_t n, unsigned attempts = FSK_READ_ATTEMPTS);

#endif // CASSETTE_FSK_READ_H
