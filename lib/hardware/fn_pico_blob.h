#ifndef FN_PICO_BLOB_H
#define FN_PICO_BLOB_H

// fn_pico_blob.h -- accessor layer for companion-MCU firmware images
// bundled into an ESP32 build by build_pico.py. The data lives in the
// generated lib/hardware/fn_pico_blob_data.cpp; the logic lives here,
// checked in and reviewable.
//
// build_pico.py writes that file for EVERY board -- real bytes, or a stub
// with fn_pico_blob_count == 0 -- so these symbols always exist and no
// call site needs an #ifdef for a particular board.
//
// ESP_PLATFORM (rather than a new FUJINET_HAS_PICO macro) means exactly
// "built by the pio build that runs build_pico.py". This matters because
// lib/media/rs232/diskTypeROM.cpp, the intended consumer, is also compiled
// into FujiNet-PC, which never generates the data file; the inline
// fallbacks below give it a harmless empty registry so the same call
// compiles in both builds.
//
// Do NOT collapse this into one set of definitions with __attribute__
// ((weak)) fallbacks. ESP-IDF links lib/ as a static archive, and a weak
// definition satisfies the reference on its own, so the archive member
// holding the strong definition is never pulled in and the registry
// silently stays empty on a board that does have a blob -- the same class
// of silent-wrong-result failure as the objcopy approach in
// build_pico.py's header. Always-generating avoids the question: there is
// exactly one definition per build.
//
// When the auto-reflash consumer lands, add this header to
// fujinet_pc.cmake's explicit lib/hardware list.

#include <cstddef>
#include <cstdint>
#include <cstring>

struct fn_pico_blob
{
    const char *name;
    const uint8_t *data;
    size_t size;
};

#ifdef ESP_PLATFORM
// Defined in the generated lib/hardware/fn_pico_blob_data.cpp.
extern "C" const fn_pico_blob fn_pico_blobs[];
extern "C" const size_t       fn_pico_blob_count;
#endif // ESP_PLATFORM

inline size_t fn_pico_blob_total()
{
#ifdef ESP_PLATFORM
    return fn_pico_blob_count;
#else
    return 0;
#endif
}

inline const fn_pico_blob *fn_pico_blob_at(size_t i)
{
#ifdef ESP_PLATFORM
    if (i >= fn_pico_blob_count)
        return nullptr;
    return &fn_pico_blobs[i];
#else
    (void)i;
    return nullptr;
#endif
}

// Look up by the name given in [fujinet] pico_artifacts (e.g. "intv_fw").
// Callers must handle nullptr -- the normal case on most boards.
inline const fn_pico_blob *fn_pico_blob_find(const char *name)
{
#ifdef ESP_PLATFORM
    for (size_t i = 0; i < fn_pico_blob_count; i++)
    {
        if (strcmp(fn_pico_blobs[i].name, name) == 0)
            return &fn_pico_blobs[i];
    }
    return nullptr;
#else
    (void)name;
    return nullptr;
#endif
}

#endif // FN_PICO_BLOB_H
