/*
 * Minimal POSIX VFS for the ESP32 SQLite3 component (SQLITE_OS_OTHER build).
 *
 * The component is compiled with SQLITE_OS_OTHER=1 and SQLITE_OMIT_AUTOINIT=1,
 * which means:
 *   - No built-in OS layer is compiled in; the app must provide one.
 *   - sqlite3_initialize() is NOT called automatically by sqlite3_open(); the
 *     caller must call it once before opening any database.
 *
 * This file provides:
 *   - A thin sqlite3_vfs wrapper around the ESP-IDF POSIX VFS (open/read/write/…)
 *   - sqlite3_os_init() / sqlite3_os_end() as required by SQLITE_OS_OTHER
 *
 * Usage (from application code):
 *     sqlite3_initialize();       // call once at startup
 *     sqlite3 *db;
 *     sqlite3_open("/sd/my.db", &db);
 */

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#include "sqlite3.h"
#include "sqlite3_esp32.h"
#include <esp_heap_caps.h>

/* ── File descriptor wrapper ─────────────────────────────────────────────── */

typedef struct {
    sqlite3_file base;   /* must be first: sqlite3 casts sqlite3_file* ↔ Esp32File* */
    int fd;
} Esp32File;

/* ── I/O methods ─────────────────────────────────────────────────────────── */

static int vfsClose(sqlite3_file *pFile)
{
    Esp32File *p = (Esp32File *)pFile;
    close(p->fd);
    p->fd = -1;
    return SQLITE_OK;
}

static int vfsRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite3_int64 iOfst)
{
    Esp32File *p = (Esp32File *)pFile;
    ssize_t got;
    if (lseek(p->fd, (off_t)iOfst, SEEK_SET) < 0) return SQLITE_IOERR_READ;
    got = read(p->fd, zBuf, (size_t)iAmt);
    if (got < 0) return SQLITE_IOERR_READ;
    if (got < iAmt) {
        memset((char *)zBuf + got, 0, (size_t)(iAmt - got));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int vfsWrite(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite3_int64 iOfst)
{
    Esp32File *p = (Esp32File *)pFile;
    if (lseek(p->fd, (off_t)iOfst, SEEK_SET) < 0) return SQLITE_IOERR_WRITE;
    if (write(p->fd, zBuf, (size_t)iAmt) != iAmt)  return SQLITE_IOERR_WRITE;
    return SQLITE_OK;
}

static int vfsTruncate(sqlite3_file *pFile, sqlite3_int64 size)
{
    Esp32File *p = (Esp32File *)pFile;
    if (ftruncate(p->fd, (off_t)size) < 0) return SQLITE_IOERR_TRUNCATE;
    return SQLITE_OK;
}

static int vfsSync(sqlite3_file *pFile, int flags)
{
    /* SQLITE_NO_SYNC=1 is compiled in; sync is a no-op */
    return SQLITE_OK;
}

static int vfsFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize)
{
    Esp32File *p = (Esp32File *)pFile;
    struct stat st;
    if (fstat(p->fd, &st) < 0) return SQLITE_IOERR_FSTAT;
    *pSize = (sqlite3_int64)st.st_size;
    return SQLITE_OK;
}

/* Single-connection, single-threaded — locking is a no-op */
static int vfsLock(sqlite3_file *pFile, int eLock)               { return SQLITE_OK; }
static int vfsUnlock(sqlite3_file *pFile, int eLock)             { return SQLITE_OK; }
static int vfsCheckReservedLock(sqlite3_file *pFile, int *pOut)  { *pOut = 0; return SQLITE_OK; }

static int vfsFileControl(sqlite3_file *pFile, int op, void *pArg) { return SQLITE_NOTFOUND; }
static int vfsSectorSize(sqlite3_file *pFile)                       { return 512; }
static int vfsDeviceCharacteristics(sqlite3_file *pFile)
{
    return SQLITE_IOCAP_SAFE_APPEND | SQLITE_IOCAP_SEQUENTIAL;
}

static const sqlite3_io_methods esp32IoMethods = {
    1,                        /* iVersion */
    vfsClose,
    vfsRead,
    vfsWrite,
    vfsTruncate,
    vfsSync,
    vfsFileSize,
    vfsLock,
    vfsUnlock,
    vfsCheckReservedLock,
    vfsFileControl,
    vfsSectorSize,
    vfsDeviceCharacteristics,
};

/* ── VFS methods ─────────────────────────────────────────────────────────── */

static int vfsOpen(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile,
                   int flags, int *pOutFlags)
{
    Esp32File *p = (Esp32File *)pFile;
    int openFlags = 0;
    int fd;

    p->base.pMethods = NULL;   /* mark as invalid until open succeeds */
    p->fd = -1;

    if (!zName) return SQLITE_IOERR;

    /* Translate SQLite flags → POSIX flags */
    if (flags & SQLITE_OPEN_READWRITE) openFlags = O_RDWR;
    else                               openFlags = O_RDONLY;
    if (flags & SQLITE_OPEN_CREATE)    openFlags |= O_CREAT;
    if (flags & SQLITE_OPEN_EXCLUSIVE) openFlags |= O_EXCL;

    fd = open(zName, openFlags, 0644);
    if (fd < 0) return SQLITE_CANTOPEN;

    p->fd = fd;
    p->base.pMethods = &esp32IoMethods;
    if (pOutFlags) *pOutFlags = flags;
    return SQLITE_OK;
}

static int vfsDelete(sqlite3_vfs *pVfs, const char *zPath, int syncDir)
{
    unlink(zPath);
    return SQLITE_OK;
}

static int vfsAccess(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut)
{
    struct stat st;
    *pResOut = (stat(zPath, &st) == 0) ? 1 : 0;
    return SQLITE_OK;
}

static int vfsFullPathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut)
{
    /* Paths are already absolute on ESP-IDF; just copy */
    strncpy(zOut, zPath, (size_t)(nOut - 1));
    zOut[nOut - 1] = '\0';
    return SQLITE_OK;
}

static int vfsRandomness(sqlite3_vfs *pVfs, int nBuf, char *zBuf)
{
    /* LCG seeded from wall clock — sufficient for SQLite's internal use */
    unsigned int seed = (unsigned int)time(NULL);
    for (int i = 0; i < nBuf; i++) {
        seed = seed * 1664525u + 1013904223u;
        zBuf[i] = (char)(seed >> 24);
    }
    return SQLITE_OK;
}

static int vfsSleep(sqlite3_vfs *pVfs, int microseconds)
{
    /* Not called in practice for single-connection use */
    return microseconds;
}

static int vfsCurrentTime(sqlite3_vfs *pVfs, double *pTime)
{
    /* Julian day number for the Unix epoch is 2440587.5 */
    *pTime = (double)time(NULL) / 86400.0 + 2440587.5;
    return SQLITE_OK;
}

static sqlite3_vfs esp32Vfs = {
    1,               /* iVersion */
    sizeof(Esp32File), /* szOsFile — SQLite allocates this much per open file */
    256,             /* mxPathname */
    NULL,            /* pNext */
    "esp32",         /* zName */
    NULL,            /* pAppData */
    vfsOpen,
    vfsDelete,
    vfsAccess,
    vfsFullPathname,
    NULL,            /* xDlOpen  — no dynamic loading */
    NULL,            /* xDlError */
    NULL,            /* xDlSym */
    NULL,            /* xDlClose */
    vfsRandomness,
    vfsSleep,
    vfsCurrentTime,
    NULL,            /* xGetLastError */
};

/* ── Required entry points for SQLITE_OS_OTHER builds ────────────────────── */

int sqlite3_os_init(void)
{
    return sqlite3_vfs_register(&esp32Vfs, 1 /* make default */);
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}

/* ── ESP32 PSRAM helpers ─────────────────────────────────────────────────── */

static int   s_esp32_inited  = 0;
static void *s_pcache_buf    = NULL;

static sqlite3_mem_methods s_system_mem;   /* saved at init time */

/* SQLite requires xSize(p) to return the exact usable bytes at p.
 * heap_caps_get_allocated_size() returns the TLSF-rounded block size which
 * may exceed the user data region and overlap the heap-poisoning tail guard,
 * causing SQLite to overwrite that guard and crash with "Bad tail" assertion.
 * Instead we prepend a 4-byte size field to every allocation so xSize can
 * return exactly what was requested, independent of heap alignment. */
static void *psram_malloc(int n)
{
    size_t *p = heap_caps_malloc((size_t)n + sizeof(size_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) return NULL;
    *p = (size_t)n;
    return p + 1;
}
static void psram_free(void *p)
{
    if (p) free((size_t *)p - 1);
}
static void *psram_realloc(void *p, int n)
{
    size_t *orig = p ? (size_t *)p - 1 : NULL;
    size_t *q = heap_caps_realloc(orig, (size_t)n + sizeof(size_t),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!q) return NULL;
    *q = (size_t)n;
    return q + 1;
}
static int psram_size(void *p)    { return p ? (int)*((size_t *)p - 1) : 0; }
static int psram_roundup(int n)   { return n; }
static int   psram_init(void *p)           { (void)p; return SQLITE_OK; }
static void  psram_shutdown(void *p)       { (void)p; }

static sqlite3_mem_methods s_psram_mem = {
    psram_malloc, psram_free, psram_realloc, psram_size,
    psram_roundup, psram_init, psram_shutdown, NULL
};

static void apply_config(sqlite3_mem_methods *mem)
{
    if (mem) sqlite3_config(SQLITE_CONFIG_MALLOC, mem);
    if (s_pcache_buf)
        sqlite3_config(SQLITE_CONFIG_PAGECACHE, s_pcache_buf, 640, 128);
}

int sqlite3_esp32_init(void)
{
    if (s_esp32_inited) return s_pcache_buf != NULL;

    s_pcache_buf = heap_caps_malloc(640 * 128, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    /* Save system mem methods before sqlite3_initialize() locks them in. */
    sqlite3_config(SQLITE_CONFIG_GETMALLOC, &s_system_mem);
    apply_config(NULL);   /* page cache only — keep system malloc for normal use */
    sqlite3_initialize();
    s_esp32_inited = 1;
    return s_pcache_buf != NULL;
}

void sqlite3_esp32_psram_malloc_enter(void)
{
    sqlite3_shutdown();
    apply_config(&s_psram_mem);
    sqlite3_initialize();
}

void sqlite3_esp32_psram_malloc_exit(void)
{
    sqlite3_shutdown();
    apply_config(&s_system_mem);
    sqlite3_initialize();
}
