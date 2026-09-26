#ifdef BUILD_MAC
#include "mediaTypeSIT.h"

#include <cstring>
#include <esp_heap_caps.h>

#include "../../stuffit/stuffit.h"
#include "../../stuffit/binhex.h"
#include "../../stuffit/ndif.h"

#include "../../include/debug.h"

#define SIT_MOUNT_MAX_IMAGE     (3u * 1024u * 1024u)   // PSRAM cap for the inner disk image
#define SIT_MOUNT_MAX_RSRC      (64u * 1024u)          // PSRAM cap for the resource fork
#define SIT_MOUNT_PROGRESS_STEP (256u * 1024u)         // Debug_printf every this many bytes

// All decoder state and scratch (Arsenic needs megabytes) goes to PSRAM
static void *sit_mount_alloc(size_t n, void *ctx)
{
    (void)ctx;
    return heap_caps_malloc(n, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
}

static void sit_mount_free(void *p, void *ctx)
{
    (void)ctx;
    if (p != nullptr)
        heap_caps_free(p);
}

static const sit_allocator sit_mount_allocator = { sit_mount_alloc, sit_mount_free, nullptr };

static bool sit_mount_has_image_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == nullptr)
        return false;
    static const char *exts[] = { ".image", ".img", ".dsk", ".dc42", ".hda", ".dmg", ".toast" };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
        if (strcasecmp(dot, exts[i]) == 0)
            return true;
    return false;
}

// sit_extract() sink: copies into a buffer sized from the entry, logs progress
struct sit_mount_sink_ctx
{
    uint8_t *buf;
    uint32_t cap;
    uint32_t pos;
    uint32_t next_progress_at;
    const char *what; // for the progress line
};

static int sit_mount_sink(const uint8_t *data, size_t n, void *vctx)
{
    sit_mount_sink_ctx *ctx = static_cast<sit_mount_sink_ctx *>(vctx);
    if (ctx->pos + n > ctx->cap)
        return -1;
    memcpy(ctx->buf + ctx->pos, data, n);
    ctx->pos += n;
    while (ctx->next_progress_at <= ctx->cap && ctx->pos >= ctx->next_progress_at)
    {
        Debug_printf("\nStuffIt: extracting %s: %u/%u bytes", ctx->what, ctx->next_progress_at, ctx->cap);
        ctx->next_progress_at += SIT_MOUNT_PROGRESS_STEP;
    }
    return 0;
}

void MediaTypeSIT::release()
{
    if (_image_fh != nullptr)
    {
        fclose(_image_fh);
        _image_fh = nullptr;
    }
    if (_image_buf != nullptr)
    {
        heap_caps_free(_image_buf);
        _image_buf = nullptr;
    }
    if (_rsrc_buf != nullptr)
    {
        heap_caps_free(_rsrc_buf);
        _rsrc_buf = nullptr;
    }
    _image_len = 0;
    _rsrc_len = 0;
    _inner_filename[0] = '\0';
    _image_type = MEDIATYPE_UNKNOWN;
}

// The image type from its bytes: DiskCopy 4.2, a raw 400K/800K dump, an HFS
// volume or a drive image. Which media serves it depends on the slot.
success_is_true MediaTypeSIT::classify()
{
    const uint8_t *b = _image_buf;
    if (_image_len >= 0x54 && b[0x52] == 0x01 && b[0x53] == 0x00)
        _image_type = MEDIATYPE_DC42;
    else if (_image_len == 409600 || _image_len == 819200 ||
             (_image_len >= 0x402 && b[0x400] == 'B' && b[0x401] == 'D') || // HFS MDB
             (_image_len >= 2 && b[0] == 'E' && b[1] == 'R'))              // drive image
        _image_type = MEDIATYPE_DSK;
    else
        _image_type = MEDIATYPE_UNKNOWN;

    RETURN_SUCCESS_IF(_image_type != MEDIATYPE_UNKNOWN);
}

// Replaces the NDIF data fork in *image_buf with the decoded image. The
// resource fork is freed as soon as ndif_open() has parsed its chunk table,
// so it is not held while both image buffers are live.
static success_is_true sit_mount_decode_ndif(uint8_t **image_buf, uint32_t *image_len,
                                             uint8_t **rsrc_buf, uint32_t *rsrc_len,
                                             const char *inner_filename)
{
    FILE *ndif_data_fh = fmemopen(*image_buf, *image_len, "rb");
    if (ndif_data_fh == nullptr)
    {
        Debug_printf("\nStuffIt: NDIF fmemopen() failed for '%s'", inner_filename);
        RETURN_ERROR_AS_FALSE();
    }

    ndif_image nd;
    int nrc = ndif_open(&nd, ndif_data_fh, *rsrc_buf, *rsrc_len, &sit_mount_allocator);

    heap_caps_free(*rsrc_buf);
    *rsrc_buf = nullptr;
    *rsrc_len = 0;

    if (nrc != NDIF_OK)
    {
        Debug_printf("\nStuffIt: NDIF decode of '%s' failed to open: %s", inner_filename, ndif_strerror(nrc));
        fclose(ndif_data_fh);
        RETURN_ERROR_AS_FALSE();
    }

    uint32_t decoded_len = nd.block_count * 512u;
    if (decoded_len == 0 || decoded_len > SIT_MOUNT_MAX_IMAGE)
    {
        Debug_printf("\nStuffIt: NDIF image '%s' decodes to %u bytes, over the %u byte PSRAM cap",
                     inner_filename, decoded_len, SIT_MOUNT_MAX_IMAGE);
        ndif_close(&nd);
        fclose(ndif_data_fh);
        RETURN_ERROR_AS_FALSE();
    }

    uint8_t *decoded_buf = static_cast<uint8_t *>(heap_caps_malloc(decoded_len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    if (decoded_buf == nullptr)
    {
        Debug_printf("\nStuffIt: no PSRAM for a %u byte decoded NDIF image ('%s')", decoded_len, inner_filename);
        ndif_close(&nd);
        fclose(ndif_data_fh);
        RETURN_ERROR_AS_FALSE();
    }

    sit_mount_sink_ctx nctx = { decoded_buf, decoded_len, 0, SIT_MOUNT_PROGRESS_STEP, "NDIF image" };
    int erc = ndif_extract(&nd, sit_mount_sink, &nctx);

    ndif_close(&nd);
    fclose(ndif_data_fh);

    if (erc != NDIF_OK)
    {
        Debug_printf("\nStuffIt: NDIF decode of '%s' failed: %s", inner_filename, ndif_strerror(erc));
        heap_caps_free(decoded_buf);
        RETURN_ERROR_AS_FALSE();
    }

    heap_caps_free(*image_buf);
    *image_buf = decoded_buf;
    *image_len = nctx.pos;
    RETURN_SUCCESS_AS_TRUE();
}

// sit_archive is tens of KB, so extract() keeps it and the entries in PSRAM,
// not on the web task's stack
static void sit_mount_free_locals(sit_archive *ar, sit_entry *e, sit_entry *best)
{
    if (ar != nullptr)
        sit_mount_allocator.free(ar, sit_mount_allocator.ctx);
    if (e != nullptr)
        sit_mount_allocator.free(e, sit_mount_allocator.ctx);
    if (best != nullptr)
        sit_mount_allocator.free(best, sit_mount_allocator.ctx);
}

success_is_true MediaTypeSIT::extract(FILE *archive_fh)
{
    const char *archive_filename = "archive"; // mount() only gets the FILE*
    release();

    Debug_printf("\nStuffIt: extract('%s') start, %u bytes free PSRAM",
                 archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    sit_archive *ar = static_cast<sit_archive *>(sit_mount_allocator.alloc(sizeof(sit_archive), sit_mount_allocator.ctx));
    sit_entry *e = static_cast<sit_entry *>(sit_mount_allocator.alloc(sizeof(sit_entry), sit_mount_allocator.ctx));
    sit_entry *best = static_cast<sit_entry *>(sit_mount_allocator.alloc(sizeof(sit_entry), sit_mount_allocator.ctx));
    if (ar == nullptr || e == nullptr || best == nullptr)
    {
        Debug_printf("\nStuffIt: no PSRAM for archive/entry state for '%s'", archive_filename);
        sit_mount_free_locals(ar, e, best);
        RETURN_ERROR_AS_FALSE();
    }

    bool looks_like_hqx = false;
    {
        char peek[80];
        if (fseek(archive_fh, 0, SEEK_SET) == 0)
        {
            size_t got = fread(peek, 1, sizeof(peek) - 1, archive_fh);
            peek[got] = '\0';
            if (strstr(peek, "This file must be converted with BinHex") != nullptr)
                looks_like_hqx = true;
        }
    }
    if (fseek(archive_fh, 0, SEEK_SET) != 0)
    {
        Debug_printf("\nStuffIt: cannot seek archive '%s'", archive_filename);
        sit_mount_free_locals(ar, e, best);
        RETURN_ERROR_AS_FALSE();
    }

    hqx_file hqx;
    bool have_hqx = false;
    FILE *hqx_fh = nullptr;
    FILE *sit_source = archive_fh;

    if (looks_like_hqx)
    {
        int hrc = hqx_open(archive_fh, &sit_mount_allocator, &hqx);
        if (hrc == SIT_OK)
        {
            have_hqx = true;
            hqx_fh = hqx_data_fork(&hqx);
            if (hqx_fh == nullptr)
            {
                Debug_printf("\nStuffIt: hqx_data_fork() (fmemopen) failed for '%s'", archive_filename);
                hqx_close(&hqx);
                sit_mount_free_locals(ar, e, best);
                RETURN_ERROR_AS_FALSE();
            }
            sit_source = hqx_fh;
        }
        else if (hrc == SIT_E_FORMAT)
        {
            // .hqx name but no BinHex banner: try it as a bare archive
            fseek(archive_fh, 0, SEEK_SET);
            sit_source = archive_fh;
        }
        else
        {
            Debug_printf("\nStuffIt: BinHex decode of '%s' failed: %s", archive_filename, sit_strerror(hrc));
            sit_mount_free_locals(ar, e, best);
            RETURN_ERROR_AS_FALSE();
        }
    }

    int rc = sit_open(ar, sit_source, &sit_mount_allocator);
    if (rc != SIT_OK)
    {
        Debug_printf("\nStuffIt: '%s' is not a recognized SIT!/StuffIt5 archive: %s",
                     archive_filename, sit_strerror(rc));
        if (hqx_fh != nullptr)
            fclose(hqx_fh);
        if (have_hqx)
            hqx_close(&hqx);
        sit_mount_free_locals(ar, e, best);
        RETURN_ERROR_AS_FALSE();
    }

    // The disk images are the entries with an image-like name, in archive
    // order, and _entry picks one (0 = the first). Without any, the largest
    // data fork is the image. sit_extract() works on a saved entry.
    int wanted = _entry > 0 ? _entry : 1;
    int images = 0;
    bool have_candidate = false;
    bool picked_image = false;

    while ((rc = sit_next_entry(ar, e)) == 1)
    {
        if (e->data_len == 0)
            continue;

        if (sit_mount_has_image_ext(e->path))
        {
            if (++images == wanted)
            {
                *best = *e;
                picked_image = true;
            }
        }
        else if (images == 0 && (!have_candidate || e->data_len > best->data_len))
        {
            *best = *e;
            have_candidate = true;
        }
    }
    if (images > 0)
        have_candidate = picked_image;
    else if (wanted > 1)
        have_candidate = false;
    _image_count = images > 0 ? images : 1;

    if (rc < 0)
    {
        Debug_printf("\nStuffIt: error walking '%s': %s", archive_filename, sit_strerror(rc));
        sit_close(ar);
        if (hqx_fh != nullptr)
            fclose(hqx_fh);
        if (have_hqx)
            hqx_close(&hqx);
        sit_mount_free_locals(ar, e, best);
        RETURN_ERROR_AS_FALSE();
    }

    if (!have_candidate)
    {
        Debug_printf("\nStuffIt: '%s' has no disk image entry", archive_filename);
        sit_close(ar);
        if (hqx_fh != nullptr)
            fclose(hqx_fh);
        if (have_hqx)
            hqx_close(&hqx);
        sit_mount_free_locals(ar, e, best);
        RETURN_ERROR_AS_FALSE();
    }

    if (best->data_len > SIT_MOUNT_MAX_IMAGE)
    {
        Debug_printf("\nStuffIt: '%s' inner image '%s' is %u bytes, over the %u byte PSRAM cap",
                     archive_filename, best->path, best->data_len, SIT_MOUNT_MAX_IMAGE);
        sit_close(ar);
        if (hqx_fh != nullptr)
            fclose(hqx_fh);
        if (have_hqx)
            hqx_close(&hqx);
        sit_mount_free_locals(ar, e, best);
        RETURN_ERROR_AS_FALSE();
    }

    sit_progress prog;
    // Resource fork first: small, but its Arsenic scratch would not fit next
    // to the image buffer. Optional; only NDIF needs it.
    if (best->rsrc_len > 0 && best->rsrc_len <= SIT_MOUNT_MAX_RSRC)
    {
        _rsrc_buf = static_cast<uint8_t *>(heap_caps_malloc(best->rsrc_len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
        if (_rsrc_buf != nullptr)
        {
            sit_mount_sink_ctx rctx = { _rsrc_buf, best->rsrc_len, 0, SIT_MOUNT_PROGRESS_STEP, "resource fork" };
            rc = sit_extract(ar, best, SIT_FORK_RSRC, sit_mount_sink, &rctx, &prog);
            if (rc == SIT_OK)
            {
                _rsrc_len = rctx.pos;
            }
            else
            {
                Debug_printf("\nStuffIt: resource fork of '%s' not extracted (%s) - continuing without it",
                             best->path, sit_strerror(rc));
                heap_caps_free(_rsrc_buf);
                _rsrc_buf = nullptr;
            }
        }
    }
    else if (best->rsrc_len > SIT_MOUNT_MAX_RSRC)
    {
        Debug_printf("\nStuffIt: resource fork of '%s' is %u bytes, over the %u byte cap - skipping",
                     best->path, best->rsrc_len, SIT_MOUNT_MAX_RSRC);
    }

    _image_buf = static_cast<uint8_t *>(heap_caps_malloc(best->data_len, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM));
    if (_image_buf == nullptr)
    {
        Debug_printf("\nStuffIt: no PSRAM for a %u byte image ('%s')", best->data_len, best->path);
        sit_close(ar);
        if (hqx_fh != nullptr)
            fclose(hqx_fh);
        if (have_hqx)
            hqx_close(&hqx);
        sit_mount_free_locals(ar, e, best);
        release(); // frees _rsrc_buf
        Debug_printf("\nStuffIt: extract('%s') end (no PSRAM for image), %u bytes free PSRAM",
                     archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        RETURN_ERROR_AS_FALSE();
    }

    sit_mount_sink_ctx dctx = { _image_buf, best->data_len, 0, SIT_MOUNT_PROGRESS_STEP, "data fork" };
    rc = sit_extract(ar, best, SIT_FORK_DATA, sit_mount_sink, &dctx, &prog);
    if (rc != SIT_OK)
    {
        Debug_printf("\nStuffIt: extracting '%s' failed: %s", best->path, sit_strerror(rc));
        sit_close(ar);
        if (hqx_fh != nullptr)
            fclose(hqx_fh);
        if (have_hqx)
            hqx_close(&hqx);
        sit_mount_free_locals(ar, e, best);
        release();
        Debug_printf("\nStuffIt: extract('%s') end (data fork extract failed), %u bytes free PSRAM",
                     archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        RETURN_ERROR_AS_FALSE();
    }
    _image_len = dctx.pos;

    sit_close(ar);
    if (hqx_fh != nullptr)
        fclose(hqx_fh);
    if (have_hqx)
        hqx_close(&hqx);

    const char *base = strrchr(best->path, '/');
    base = (base != nullptr) ? base + 1 : best->path;
    strncpy(_inner_filename, base, sizeof(_inner_filename) - 1);
    _inner_filename[sizeof(_inner_filename) - 1] = '\0';

    sit_mount_free_locals(ar, e, best);

    if (_rsrc_buf != nullptr && ndif_probe(_rsrc_buf, _rsrc_len))
    {
        Debug_printf("\nStuffIt: '%s' is an NDIF (Disk Copy 6) image - decoding, %u bytes free PSRAM",
                     _inner_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        if (sit_mount_decode_ndif(&_image_buf, &_image_len, &_rsrc_buf, &_rsrc_len, _inner_filename).is_error())
        {
            release();
            Debug_printf("\nStuffIt: extract('%s') end (NDIF decode failed), %u bytes free PSRAM",
                         archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
            RETURN_ERROR_AS_FALSE();
        }
    }
    if (_rsrc_buf != nullptr) // not NDIF: the resource fork is no longer needed
    {
        heap_caps_free(_rsrc_buf);
        _rsrc_buf = nullptr;
        _rsrc_len = 0;
    }

    if (classify().is_error())
    {
        Debug_printf("\nStuffIt: cannot classify inner image '%s' (%u bytes) from '%s' - not DC42, "
                     "not 400K/800K, not HFS, not a drive image",
                     _inner_filename, _image_len, archive_filename);
        release();
        Debug_printf("\nStuffIt: extract('%s') end (classify failed), %u bytes free PSRAM",
                     archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        RETURN_ERROR_AS_FALSE();
    }

    _image_fh = fmemopen(_image_buf, _image_len, "rb+");
    if (_image_fh == nullptr)
    {
        Debug_printf("\nStuffIt: fmemopen() failed for extracted image '%s'", _inner_filename);
        release();
        Debug_printf("\nStuffIt: extract('%s') end (fmemopen failed), %u bytes free PSRAM",
                     archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        RETURN_ERROR_AS_FALSE();
    }

    Debug_printf("\nStuffIt: extract('%s') end (ok), %u bytes free PSRAM",
                 archive_filename, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    RETURN_SUCCESS_AS_TRUE();
}

mediatype_t MediaTypeSIT::mount(FILE *f, uint32_t disksize)
{
    success_is_true extracted = extract(f);
    fclose(f); // the image in PSRAM replaces the archive file
    if (extracted.is_error())
        return MEDIATYPE_UNKNOWN;

    _inner.reset(MediaType::create(_image_type, _floppy_slot));
    if (_inner == nullptr)
    {
        Debug_printf("\nStuffIt: '%s' cannot go in this slot", _inner_filename);
        release();
        return MEDIATYPE_UNKNOWN;
    }
    _inner->set_readonly(_readonly);
    if (_inner->mount(_image_fh, _image_len) == MEDIATYPE_UNKNOWN)
    {
        Debug_printf("\nStuffIt: '%s' did not mount", _inner_filename);
        _inner.reset(); // closed _image_fh
        _image_fh = nullptr;
        release();
        return MEDIATYPE_UNKNOWN;
    }
    Debug_printf("\nStuffIt: mounted '%s' (%u bytes)", _inner_filename, _image_len);

    num_blocks = _inner->num_blocks;
    num_sides = _inner->num_sides;
    optimal_bit_timing = _inner->optimal_bit_timing;
    _mediatype = MEDIATYPE_SIT;
    return MEDIATYPE_SIT;
}

void MediaTypeSIT::unmount()
{
    if (_inner != nullptr)
    {
        _inner->unmount(); // closes _image_fh; fmemopen does not free the buffer
        _inner.reset();
        _image_fh = nullptr;
    }
    release();
}

MediaTypeSIT::~MediaTypeSIT()
{
    unmount();
}

#endif // BUILD_MAC
