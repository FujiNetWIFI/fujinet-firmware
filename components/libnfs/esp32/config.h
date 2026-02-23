/* config.h - ESP-IDF/ESP32 configuration */

/* ESP32 Platform */
#define ESP_PLATFORM 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
/* #undef HAVE_ARPA_INET_H */

/* Whether we have clock_gettime */
/* #undef HAVE_CLOCK_GETTIME */

/* Whether gnutls exports the function gnutls_transport_is_ktls_enabled() */
/* #undef HAVE_GNUTLS_TRANSPORT_IS_KTLS_ENABLED */

/* Whether pthread library is present */
#define HAVE_PTHREAD

/* Define to 1 if you have the <dlfcn.h> header file. */
/* #undef HAVE_DLFCN_H */

/* Define to 1 if you have the <fuse.h> header file. */
/* #undef HAVE_FUSE_H */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H

/* Define to 1 if you have the `nsl' library (-lnsl). */
/* #undef HAVE_LIBNSL */

/* Define to 1 if you have the `socket' library (-lsocket). */
/* #undef HAVE_LIBSOCKET */

/* Define to 1 if you have the <memory.h> header file. */
/* #undef HAVE_MEMORY_H */

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H

/* Define to 1 if you have the <net/if.h> header file. */
/* #undef HAVE_NET_IF_H */

/* Define to 1 if you have the <poll.h> header file. */
#define HAVE_POLL_H

/* Define to 1 if you have the <pwd.h> header file. */
/* #undef HAVE_PWD_H */

/* Whether sockaddr struct has sa_len */
/* #undef HAVE_SOCKADDR_LEN */

/* Whether we have sockaddr_Storage - already provided by lwip */
#define HAVE_SOCKADDR_STORAGE

/* Whether our sockets support SO_BINDTODEVICE */
/* #undef HAVE_SO_BINDTODEVICE */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H

/* Define to 1 if you have the <stdatomic.h> header file. */
#define HAVE_STDATOMIC_H

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H

/* Define to 1 if `st_mtim.tv_nsec' is a member of `struct stat'. */
/* #undef HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC */

/* Define to 1 if you have the <sys/filio.h> header file. */
/* #undef HAVE_SYS_FILIO_H */

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H

/* Define to 1 if you have the <sys/sockio.h> header file. */
/* #undef HAVE_SYS_SOCKIO_H */

/* Define to 1 if you have the <sys/statvfs.h> header file. */
/* #undef HAVE_SYS_STATVFS_H */
/* ESP32: We define statvfs struct in libnfs.h for ESP_PLATFORM */
#define HAVE_STATVFS 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H

/* Define to 1 if you have the <sys/sysmacros.h> header file. */
/* #undef HAVE_SYS_SYSMACROS_H */

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H

/* ESP32 specific defines */
#ifdef ESP_PLATFORM
#ifndef IPPORT_RESERVED
#define IPPORT_RESERVED 1024
#endif
#endif

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H

/* Define to 1 if you have the <sys/vfs.h> header file. */
/* #undef HAVE_SYS_VFS_H */

/* Whether we have talloc nad tevent support */
/* #undef HAVE_TALLOC_TEVENT */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H

/* Define to 1 if you have the <utime.h> header file. */
#define HAVE_UTIME_H

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H

/* Define to 1 if you have the <sys/utsname.h> header file. */
#define HAVE_SYS_UTSNAME_H

/* Define to 1 if `major', `minor', and `makedev' are declared in <mkdev.h>.
   */
/* #undef MAJOR_IN_MKDEV */

/* Define to 1 if `major', `minor', and `makedev' are declared in
   <sysmacros.h>. */
/* #undef MAJOR_IN_SYSMACROS */

/* Enable large inode numbers on Mac OS X 10.5.  */
// #ifndef _DARWIN_USE_64_BIT_INODE
// # define _DARWIN_USE_64_BIT_INODE 1
// #endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to 1 if you have the <dispatch/dispatch.h> header file. */
// #define HAVE_DISPATCH_DISPATCH_H

/* Define to 1 if pthread_threadid_np() exists. */
// #define HAVE_PTHREAD_THREADID_NP

/* ESP32 compatibility layer */
#ifdef ESP_PLATFORM
#include "esp32/esp32_compat.h"
#endif
