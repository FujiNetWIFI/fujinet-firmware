#ifndef COMPAT_INET_H
#define COMPAT_INET_H

#include <errno.h>

#if defined(_WIN32)

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef uint32_t in_addr_t;

#else

#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <sys/socket.h>

#ifndef closesocket
#include <unistd.h>
#define closesocket(x) ::close(x)
#endif

#endif

#ifndef ESP_PLATFORM
/* borrowed from lwip/ip4_addr.h */
/** 255.255.255.255 */
#define IPADDR_NONE         ((uint32_t)0xffffffffUL)
/** 127.0.0.1 */
#define IPADDR_LOOPBACK     ((uint32_t)0x7f000001UL)
/** 0.0.0.0 */
#define IPADDR_ANY          ((uint32_t)0x00000000UL)
/** 255.255.255.255 */
#define IPADDR_BROADCAST    ((uint32_t)0xffffffffUL)
#endif // !ESP_PLATFORM


#ifdef __cplusplus
extern "C" {
#endif

/* takes in_addr_t as argument */
char *compat_inet_ntoa(in_addr_t in);

static inline int compat_getsockerr()
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

static inline void compat_setsockerr(int err)
{
#if defined(_WIN32)
    WSASetLastError(err);
#else
    errno = err;
#endif
}

const char *compat_sockstrerror(int err);

#ifdef __cplusplus
}
#endif


#endif // COMPAT_INET_H