#ifndef LIBSSH_COMPAT_STUBS_H
#define LIBSSH_COMPAT_STUBS_H

#include <net/if.h>
#include <pwd.h>
#include <glob.h>

#if !defined(GLOB_NOMATCH)
#define GLOB_NOMATCH -3
#endif

#define gai_strerror(ecode) "unknown error"
#define getuid() getuid_stub() (uid_t)0

static inline int getpwuid_r_stub(struct passwd **result) { errno = ENOENT; *result = NULL; return 1; }
#define getpwuid_r(uid,passwd,buffer,buflen,result) getpwuid_r_stub(result)

static inline struct passwd *getpwnam_stub() { errno = ENOENT; return NULL; }
#define getpwnam(name) getpwnam_stub()

static inline int gethostname_stub(char *name, size_t namelen) {strlcpy(name, "esp32", namelen); return 0; }
#define gethostname(name, namelen) gethostname_stub(name, namelen)

static inline pid_t waitpid_stub() { errno = ENOSYS; return (pid_t)-1; }
#define waitpid(pid,status,options) waitpid_stub()

static inline int glob_stub() { errno = ENOENT; return GLOB_NOMATCH; }
#define glob(pattern, flags, errfunc, pglob) glob_stub()

#define globfree(pglob) do { } while(0)


static inline int socketpair_stub() { errno = ENOSYS; return -1; }
#define socketpair(d, type, protocol, sv) socketpair_stub()

#endif