/* ESP32 compatibility layer for libnfs */

#ifndef _ESP32_COMPAT_H_
#define _ESP32_COMPAT_H_

#ifdef ESP_PLATFORM

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device number macros - simplified for ESP32 */
#ifndef major
#define major(dev) ((int)(((unsigned int)(dev) >> 8) & 0xff))
#endif

#ifndef minor
#define minor(dev) ((int)((unsigned int)(dev) & 0xff))
#endif

#ifndef makedev
#define makedev(maj, min) (((maj) << 8) | (min))
#endif

/* User/Group ID stubs - ESP32 doesn't have multi-user support */
static inline uid_t getuid(void) { return 0; }
static inline gid_t getgid(void) { return 0; }

/* Signal handling stub - ESP32/FreeRTOS doesn't support POSIX signals */
typedef void (*sig_t)(int);
static inline sig_t signal(int signum, sig_t handler) { 
    (void)signum; 
    (void)handler; 
    return NULL; 
}

/* File descriptor operations stub */
static inline int dup2(int oldfd, int newfd) {
    (void)oldfd;
    (void)newfd;
    return -1; /* Not supported on ESP32 */
}

/* Network service lookup stub */
struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};

static inline struct servent *getservbyport(int port, const char *proto) {
    (void)port;
    (void)proto;
    return NULL; /* Not supported on ESP32 */
}

#ifdef __cplusplus
}
#endif

#endif /* ESP_PLATFORM */

#endif /* _ESP32_COMPAT_H_ */
