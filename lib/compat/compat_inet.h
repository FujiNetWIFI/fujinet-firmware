#ifndef COMPAT_INET_H
#define COMPAT_INET_H

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

#ifdef __cplusplus
extern "C" {
#endif

/* takes in_addr_t as argument */
char *compat_inet_ntoa(in_addr_t in);

int compat_getsockerr();
void compat_setsockerr(int err);
const char *compat_sockstrerror(int err);

#ifdef __cplusplus
}
#endif


#endif // COMPAT_INET_H