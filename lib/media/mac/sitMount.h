/*
 * sitMount.h - unpack a StuffIt/BinHex/MacBinary archive into a PSRAM
 * disk image that macFloppy::mount() mounts like a plain host file.
 */
#ifndef _SIT_MOUNT_H
#define _SIT_MOUNT_H
#ifdef BUILD_MAC

#include <cstdint>
#include <cstdio>
#include "global_types.h"
#include "mediaType.h"

// What the chosen entry contains, judged from its bytes rather than its name
enum class sit_image_kind_t
{
    UNKNOWN,
    FLOPPY, // DiskCopy 4.2 or a raw 400K/800K sector dump
    HD20    // HFS volume or drive image
};

// One archive-backed mount. image_fh is handed to the media object, whose
// unmount() fclose()s it; fmemopen() does not free the buffer, so the owner
// clears image_fh after that and lets release() free the buffers.
class SitMount
{
public:
    SitMount() {}
    ~SitMount() { release(); }

    // Pick the disk image inside archive_fh and unpack it. archive_fh stays
    // open and owned by the caller. On error everything is released.
    success_is_true extract(FILE *archive_fh, const char *archive_filename);

    // Frees the buffers and closes image_fh if it is still set; idempotent.
    void release();

    FILE *image_fh = nullptr;              // fmemopen(image_buf, image_len, "rb+")
    uint8_t *image_buf = nullptr;          // PSRAM
    uint32_t image_len = 0;
    char inner_filename[256] = {0};        // basename of the entry inside the archive
    sit_image_kind_t kind = sit_image_kind_t::UNKNOWN;
    mediatype_t disk_type = MEDIATYPE_UNKNOWN; // MEDIATYPE_DC42 or MEDIATYPE_DSK

    // For the web UI
    const char *archive_kind = "";  // sit_format_name(), or "BinHex + StuffIt"
    const char *method_name = "";   // compression method of the image's data fork
    bool was_ndif = false;          // image_buf holds a decoded Disk Copy 6 image

private:
    uint8_t *rsrc_buf = nullptr;           // resource fork, only kept to decode NDIF
    uint32_t rsrc_len = 0;

    success_is_true classify();
};

#endif // BUILD_MAC
#endif // _SIT_MOUNT_H
