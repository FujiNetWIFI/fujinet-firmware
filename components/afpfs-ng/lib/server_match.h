#ifndef __SERVER_MATCH_H_
#define __SERVER_MATCH_H_

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Same family, address and port.  Not a memcmp: padding and sin_len vary. */
int afp_sockaddr_equal(const struct sockaddr * a, const struct sockaddr * b);

/* Is addr one of the addresses in a resolved list? */
int afp_addrinfo_has(const struct addrinfo * list, const struct sockaddr * addr);

/* NULL is the empty (guest) user. */
int afp_same_user(const char * a, const char * b);

/* A login is shared only once it has completed, and only with its own user. */
int afp_login_reusable(int logged_in, const char * have_user,
	const char * want_user);

#ifdef __cplusplus
}
#endif

#endif
