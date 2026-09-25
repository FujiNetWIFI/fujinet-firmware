/*
 * stuffit.h - public API for the StuffIt archive reader/decompressor.
 *
 * Pure C99, host- and target-portable: parses both the classic "SIT!"
 * archive format and the StuffIt 5 ("StuffIt (c)1997-") format, and
 * decompresses either fork (data or resource) of any entry via
 * sit_extract()'s fork selector - raw resource-fork *bytes* are in
 * scope (e.g. for a NDIF "bcem" block map), interpreting resource-fork
 * *contents* is not. No globals, no recursion, no alloca; every scratch
 * allocation the decompressors need goes through the caller-supplied
 * sit_allocator so a constrained target (e.g. ESP32 PSRAM) can control
 * memory placement and failure behavior.
 *
 * Ported from The Unarchiver's XADMaster sources (LGPL v2.1+) - see the
 * individual sit_*.c files for which reference file each was derived
 * from. This header has no upstream equivalent; it is original to this
 * project.
 */
#ifndef FN_STUFFIT_H
#define FN_STUFFIT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------- */
/* Error codes                                                          */
/* ------------------------------------------------------------------- */
enum {
    SIT_OK              = 0,
    SIT_E_IO             = -1,  /* short read / fseek failure on the FILE* */
    SIT_E_FORMAT         = -2,  /* not a recognized SIT!/StuffIt5 archive */
    SIT_E_CRC            = -3,  /* header or data-fork CRC-16 mismatch */
    SIT_E_UNSUPPORTED    = -4,  /* recognized but unimplemented compression method */
    SIT_E_ENCRYPTED      = -5,  /* entry (or archive) is encrypted; decryption is out of scope */
    SIT_E_NOMEM          = -6,  /* sit_allocator returned NULL */
    SIT_E_CORRUPT        = -7,  /* malformed compressed stream (bad code, bad marker, etc.) */
    SIT_E_INVAL          = -8,  /* bad argument / API misuse */
    SIT_E_EOF            = -9,  /* sit_next_entry called after archive exhausted (not itself an error) */
    SIT_E_LIMIT          = -10  /* an internal fixed-size table (folder depth, dir table) overflowed */
};

const char *sit_strerror(int code);

/* Human-readable name for a data/resource fork compression method code
 * (0/1/2/3/13/15, see sit_extract()'s dispatch); "?" for anything else. */
const char *sit_method_name(uint8_t method);

/* ------------------------------------------------------------------- */
/* Allocator                                                            */
/* ------------------------------------------------------------------- */
typedef struct {
    void *(*alloc)(size_t n, void *ctx);
    void  (*free)(void *p, void *ctx);
    void *ctx;
} sit_allocator;

/* Convenience allocator backed by the C library malloc/free, for hosts
 * (and the unsit CLI) that don't need custom placement. */
extern const sit_allocator sit_libc_allocator;

/* ------------------------------------------------------------------- */
/* Entry metadata                                                       */
/* ------------------------------------------------------------------- */
#define SIT_MAX_PATH   256

typedef struct {
    char path[SIT_MAX_PATH];   /* folder-prefixed, '/'-separated, raw bytes as stored */
    char type[5];               /* 4-char OSType + NUL, may be all-zero */
    char creator[5];             /* 4-char OSType + NUL, may be all-zero */

    /* An entry independently carries zero, one, or both forks; either
     * fork can independently be zero-length, use its own compression
     * method, or be encrypted (see SIT_FLAG_ENCRYPTED /
     * SIT_FLAG_RSRC_ENCRYPTED). sit_next_entry() always reports both
     * fork descriptors for every real file entry, even when a fork is
     * absent (len==0, comp_len==0) - callers that only care about one
     * fork can just ignore the other's fields. */

    uint8_t data_method;         /* compression method (masked, 0-15) for the data fork */
    uint32_t data_comp_len;      /* compressed size of the data fork, bytes */
    uint32_t data_len;           /* decompressed size of the data fork, bytes */
    uint16_t data_crc;           /* stored CRC-16/ARC of the decompressed data fork
                                   * (meaningless/stale when data_method == 15, see sit_arsenic.c) */
    long data_offset;            /* absolute FILE* offset of the compressed data-fork bytes */

    uint8_t rsrc_method;          /* compression method (masked, 0-15) for the resource fork */
    uint32_t rsrc_comp_len;       /* compressed size of the resource fork, bytes */
    uint32_t rsrc_len;            /* decompressed size of the resource fork, bytes, 0 if none */
    uint16_t rsrc_crc;            /* stored CRC-16/ARC of the decompressed resource fork
                                    * (meaningless/stale when rsrc_method == 15, see sit_arsenic.c) */
    long rsrc_offset;             /* absolute FILE* offset of the compressed resource-fork bytes -
                                    * always immediately before data_offset in both formats */

    uint32_t cdate;               /* creation date, seconds since 1904-01-01 (Mac epoch), raw/unconverted */
    uint32_t mdate;               /* modification date, same epoch */

    int flags;                   /* bit 0 (SIT_FLAG_ENCRYPTED) = data fork encrypted;
                                   * bit 1 (SIT_FLAG_RSRC_ENCRYPTED) = resource fork encrypted -
                                   * the two are independent per the format. */
} sit_entry;

#define SIT_FLAG_ENCRYPTED      0x01
#define SIT_FLAG_RSRC_ENCRYPTED 0x02

/* Fork selector for sit_extract(). */
typedef enum {
    SIT_FORK_DATA = 0,
    SIT_FORK_RSRC = 1
} sit_fork;

typedef struct {
    uint32_t bytes_out;
    uint32_t bytes_total;
    uint32_t block_size;   /* Arsenic only; 0 until the first block header has been read */
} sit_progress;

typedef int (*sit_sink_fn)(const uint8_t *buf, size_t n, void *ctx);

/* ------------------------------------------------------------------- */
/* Archive handle                                                       */
/*                                                                       */
/* The struct is fully defined here so callers can give sit_open a      */
/* pointer to storage they own (stack, static, or heap) without an      */
/* extra allocation step; treat every field as private and reachable    */
/* only through the API below - layout may change without notice.      */
/* ------------------------------------------------------------------- */
#define SIT_MAX_FOLDER_DEPTH 16
#define SIT_MAX_NAME         64
#define SIT_MAX_DIR_TABLE    192

typedef enum {
    SIT_FMT_UNKNOWN = 0,
    SIT_FMT_CLASSIC = 1,
    SIT_FMT_SIT5    = 2
} sit_format;

/* Human-readable name for a sit_format value ("StuffIt 5", "SIT!", "?"). */
const char *sit_format_name(sit_format fmt);

/* classic format: a simple folder-name stack, pushed/popped as
 * StuffItStartFolder/StuffItEndFolder markers are consumed. */
typedef struct {
    char name[SIT_MAX_FOLDER_DEPTH][SIT_MAX_NAME];
    int depth;
} sit_classic_state;

/* StuffIt5 format: directories are addressed by the file offset of
 * their own header record ("diroffs"); children declare that offset
 * as their parent, so a small offset->path table replaces recursion. */
typedef struct {
    long offset;                 /* header offset of this directory (its "diroffs" key) */
    char path[SIT_MAX_PATH];     /* resolved folder path, '/'-separated, "" for root */
} sit_dir_table_entry;

typedef struct {
    long base;                   /* archive base offset (offset of the "StuffIt (c)..." banner) */
    long numentries;             /* growing loop bound (numfiles, plus each directory's own numfiles) */
    long index;                  /* current iteration index into the growing loop */
    sit_dir_table_entry dirs[SIT_MAX_DIR_TABLE];
    int ndirs;
} sit_sit5_state;

struct sit_archive {
    FILE *f;
    sit_allocator alloc;
    sit_format format;
    long base;                   /* absolute offset the archive starts at (normally 0) */
    long nextpos;                 /* absolute offset of the next raw entry header to parse -
                                    * tracked explicitly (not just "current FILE* position")
                                    * because sit_extract() seeks the FILE* elsewhere to pull
                                    * compressed fork data between sit_next_entry() calls. */
    int done;                    /* 1 once sit_next_entry has reported end-of-archive */
    int error;                   /* sticky fatal error code, or SIT_OK */

    long totalsize;               /* classic: archive size, from the archive header */

    union {
        sit_classic_state classic;
        sit_sit5_state sit5;
    } u;
};
typedef struct sit_archive sit_archive;

/* ------------------------------------------------------------------- */
/* API                                                                   */
/* ------------------------------------------------------------------- */

/* Identify and open the archive at the FILE*'s current position (which
 * must be the start of the archive, offset 0 for a bare .sit file).
 * Does not take ownership of f - caller closes it after sit_close(). */
int sit_open(sit_archive *ar, FILE *f, const sit_allocator *a);

/* Advance to the next non-folder entry. Returns 1 with *e filled in,
 * 0 at end of archive, or a negative SIT_E_* code on a fatal parse
 * error (corrupt header, CRC mismatch, table overflow, I/O error).
 * Encrypted entries are still reported (with SIT_FLAG_ENCRYPTED set)
 * rather than treated as a fatal error - only sit_extract() refuses to
 * decrypt them. */
int sit_next_entry(sit_archive *ar, sit_entry *e);

/* Decompress one fork of e (SIT_FORK_DATA or SIT_FORK_RSRC), delivering
 * it to sink in chunks. The two forks are extracted independently -
 * same dispatch, just different method/offset/comp_len/len fields off
 * e. A zero-length fork (len==0 and comp_len==0, e.g. a data fork on a
 * resource-fork-only entry, or an entry with no resource fork at all)
 * succeeds trivially without calling sink. Returns SIT_OK on success
 * (including a verified CRC-16 for every method except 15/Arsenic,
 * whose stored CRC field is stale in the reference implementation and
 * is therefore not checked), or a negative SIT_E_* code: SIT_E_INVAL
 * (bad `which`), SIT_E_ENCRYPTED, SIT_E_UNSUPPORTED (method not
 * implemented), SIT_E_CRC, SIT_E_CORRUPT, SIT_E_NOMEM, SIT_E_IO. prog
 * may be NULL. */
int sit_extract(sit_archive *ar, const sit_entry *e, sit_fork which,
                 sit_sink_fn sink, void *ctx, sit_progress *prog);

void sit_close(sit_archive *ar);

/* Which archive format sit_open() recognized (classic "SIT!" or
 * StuffIt 5); for callers (e.g. the web UI) that want to report it
 * without reaching into the "private" struct. */
sit_format sit_get_format(const sit_archive *ar);

#ifdef __cplusplus
}
#endif

#endif /* FN_STUFFIT_H */
