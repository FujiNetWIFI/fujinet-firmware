#ifndef FN_PICO_BLOB_H
#define FN_PICO_BLOB_H

// fn_pico_blob.h -- accessor layer for companion-MCU firmware images
// bundled into an ESP32 build by build_pico.py. The data lives in the
// generated fn_pico_blob_data.cpp in the build dir; the logic lives here,
// checked in and reviewable.
//
// build_pico.py writes that file for EVERY board -- real bytes, or a stub
// with fn_pico_blob_count == 0 -- so these symbols always exist and no
// call site needs an #ifdef for a particular board.
//
// Consumers are PicoUpdater and PicobootClient, ESP-only behind
// CONFIG_USB_PICOBOOT_HOST_ENABLED; nothing on FujiNet-PC includes this.
//
// Do NOT collapse this into one set of definitions with __attribute__
// ((weak)) fallbacks. ESP-IDF links lib/ as a static archive, and a weak
// definition satisfies the reference on its own, so the archive member
// holding the strong definition is never pulled in and the registry
// silently stays empty on a board that does have a blob -- the same class
// of silent-wrong-result failure as the objcopy approach in
// build_pico.py's header. Always-generating avoids the question: there is
// exactly one definition per build.

#include <cstddef>
#include <cstdint>
#include <cstring>

// Which companion chip an image is for. RP2040 and RP2350 differ in ways the
// flashing code cannot paper over: they enumerate in BOOTSEL under different
// USB product IDs, and they reboot with different PICOBOOT commands
// (PC_REBOOT vs PC_REBOOT2). RP2354 is an RP2350 die with stacked flash and
// is FN_PICO_CHIP_RP2350 here. Values must match PICO_CHIPS in build_pico.py.
#define FN_PICO_CHIP_UNKNOWN 0
#define FN_PICO_CHIP_RP2040  1
#define FN_PICO_CHIP_RP2350  2

struct fn_pico_blob
{
    const char *name;    // [fujinet] pico_artifacts name; also its NVS key, <= 15 chars
    const uint8_t *data;
    size_t size;

    // Hex sha256 of data[0..size), computed at build time. The updater
    // compares it against what it recorded in NVS after the last successful
    // flash to decide whether the companion already runs this image, so it
    // has to be a build-time constant -- hashing the rodata at every boot
    // would spend time deriving something the build already knew.
    const char *sha256;

    // XIP address range the image occupies. flash_base is where it is
    // written; flash_limit, when non-zero, is a hard ceiling the erase and
    // write must stay below -- the Intellivision cart keeps a LittleFS of
    // user ROMs and saves above its image, and overrunning it is data loss.
    uint32_t flash_base;
    uint32_t flash_limit;

    uint8_t chip;        // FN_PICO_CHIP_*
};

// Defined in the generated fn_pico_blob_data.cpp.
extern "C" const fn_pico_blob fn_pico_blobs[];
extern "C" const size_t       fn_pico_blob_count;

inline size_t fn_pico_blob_total()
{
    return fn_pico_blob_count;
}

inline const fn_pico_blob *fn_pico_blob_at(size_t i)
{
    if (i >= fn_pico_blob_count)
        return nullptr;
    return &fn_pico_blobs[i];
}

// Look up by the name given in [fujinet] pico_artifacts (e.g. "intv_fw").
// Callers must handle nullptr -- the normal case on most boards.
inline const fn_pico_blob *fn_pico_blob_find(const char *name)
{
    for (size_t i = 0; i < fn_pico_blob_count; i++)
    {
        if (strcmp(fn_pico_blobs[i].name, name) == 0)
            return &fn_pico_blobs[i];
    }
    return nullptr;
}

#endif // FN_PICO_BLOB_H
