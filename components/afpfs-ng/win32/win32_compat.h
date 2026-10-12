/* Windows compatibility layer for afpfs-ng, force-included into every source
 * file the way esp32/esp32_compat.h is on ESP32. */

#ifndef _AFPFS_WIN32_COMPAT_H_
#define _AFPFS_WIN32_COMPAT_H_

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

typedef unsigned int uid_t;
typedef unsigned int gid_t;

#define SHUT_RDWR SD_BOTH
#define bzero(p,n) memset((p),0,(n))
#define bcmp(a,b,n) memcmp((a),(b),(n))

struct passwd {
    char *pw_name;
    char *pw_passwd;
    uid_t pw_uid;
    gid_t pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct group {
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char **gr_mem;
};

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    unsigned long f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
struct passwd *getpwuid(uid_t uid);

/* sigmask is ignored; the loop is built with AFPFS_POLL_LOOP. */
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const void *sigmask);

#endif /* _WIN32 */

#endif
