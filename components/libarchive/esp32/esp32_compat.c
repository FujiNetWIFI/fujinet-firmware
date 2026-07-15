/*
 * ESP32 POSIX stubs for archive_write_disk_posix.c.
 *
 * ESP32's newlib does not implement the POSIX *at() functions, ownership
 * operations, or several filesystem primitives.  This file provides minimal
 * stubs so the linker is satisfied.
 *
 * FAT32/LittleFS on ESP32 have no concept of Unix permissions, symlinks,
 * FIFOs, or hard links, so the ownership/mode stubs are safe no-ops.
 * The *at() variants forward to their non-at equivalents when called with
 * AT_FDCWD (the only case libarchive triggers during normal extraction),
 * and return ENOSYS otherwise.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

/* ── Ownership / permissions (no-ops on FAT/LittleFS) ─────────────────────── */

mode_t umask(mode_t mask)
{
    (void)mask;
    return 0;
}

int fchown(int fd, uid_t uid, gid_t gid)
{
    (void)fd; (void)uid; (void)gid;
    return 0;
}

int lchown(const char *path, uid_t uid, gid_t gid)
{
    (void)path; (void)uid; (void)gid;
    return 0;
}

int fchmod(int fd, mode_t mode)
{
    (void)fd; (void)mode;
    return 0;
}

int fchdir(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

/* ── Timestamps (best-effort; FAT supports mtime via utime) ───────────────── */

int futimes(int fd, const struct timeval tv[2])
{
    (void)fd; (void)tv;
    return 0;
}

int lutimes(const char *path, const struct timeval tv[2])
{
    (void)path; (void)tv;
    return 0;
}

/* ── Unsupported filesystem objects ───────────────────────────────────────── */

int symlink(const char *target, const char *linkpath)
{
    (void)target; (void)linkpath;
    errno = ENOSYS;
    return -1;
}

int mkfifo(const char *path, mode_t mode)
{
    (void)path; (void)mode;
    errno = ENOSYS;
    return -1;
}

int linkat(int olddirfd, const char *oldpath,
           int newdirfd, const char *newpath, int flags)
{
    (void)olddirfd; (void)oldpath; (void)newdirfd; (void)newpath; (void)flags;
    errno = ENOSYS;
    return -1;
}

/* ── *at() variants — forward to base calls when dirfd == AT_FDCWD ────────── */

int openat(int dirfd, const char *path, int flags, ...)
{
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (mode_t)va_arg(ap, int);
    va_end(ap);
    return open(path, flags, mode);
}

int fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
    (void)flags;
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return stat(path, buf);
}

int unlinkat(int dirfd, const char *path, int flags)
{
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return (flags & AT_REMOVEDIR) ? rmdir(path) : unlink(path);
}
