/*
 *  server_match.c
 *
 *  Decides whether an existing server login can serve a new request.
 *  Kept free of the rest of the library so it can be tested alone.
 */

#include <string.h>

#include "server_match.h"

int afp_sockaddr_equal(const struct sockaddr * a, const struct sockaddr * b)
{
	if ((a==NULL) || (b==NULL)) return 0;
	if (a->sa_family!=b->sa_family) return 0;

	switch (a->sa_family) {
	case AF_INET: {
		const struct sockaddr_in * a4=(const struct sockaddr_in *) a;
		const struct sockaddr_in * b4=(const struct sockaddr_in *) b;
		return (a4->sin_port==b4->sin_port) &&
			(a4->sin_addr.s_addr==b4->sin_addr.s_addr);
	}
	case AF_INET6: {
		const struct sockaddr_in6 * a6=(const struct sockaddr_in6 *) a;
		const struct sockaddr_in6 * b6=(const struct sockaddr_in6 *) b;
		return (a6->sin6_port==b6->sin6_port) &&
			(memcmp(&a6->sin6_addr,&b6->sin6_addr,
				sizeof(a6->sin6_addr))==0);
	}
	}
	return 0;
}

int afp_addrinfo_has(const struct addrinfo * list, const struct sockaddr * addr)
{
	const struct addrinfo * ai;

	for (ai=list;ai;ai=ai->ai_next)
		if (afp_sockaddr_equal(ai->ai_addr,addr)) return 1;
	return 0;
}

int afp_same_user(const char * a, const char * b)
{
	if (a==NULL) a="";
	if (b==NULL) b="";
	return strcmp(a,b)==0;
}

int afp_login_reusable(int logged_in, const char * have_user,
	const char * want_user)
{
	return logged_in && afp_same_user(have_user,want_user);
}
