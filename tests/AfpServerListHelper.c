#include <stdio.h>
#include <string.h>

#include "afp.h"

static struct addrinfo * resolve(int port)
{
	struct addrinfo hints, * res=NULL;
	char service[8];

	memset(&hints,0,sizeof(hints));
	hints.ai_family=AF_INET;
	hints.ai_socktype=SOCK_STREAM;
	snprintf(service,sizeof(service),"%d",port);
	if (getaddrinfo("127.0.0.1",service,&hints,&res)!=0) return NULL;
	return res;
}

/* Connected but not logged in: the state of a status probe. */
struct afp_server * helper_connect(int port)
{
	struct afp_server * s;

	/* The library logs through this table and it starts out NULL. */
	libafpclient_register(NULL);
	s=afp_server_init(resolve(port));

	if (s==NULL) return NULL;
	if (afp_server_connect(s,0)!=0) return NULL;
	return s;
}

void helper_log_in(struct afp_server * s, const char * user, const char * signature)
{
	strncpy(s->username,user,sizeof(s->username)-1);
	memcpy(s->signature,signature,AFP_SIGNATURE_LEN);
	s->logged_in=1;
}

struct afp_server * helper_find_by_address(int port, const char * user)
{
	struct addrinfo * address=resolve(port);
	struct afp_server * s=find_server_by_address(address,user);

	freeaddrinfo(address);
	return s;
}

struct afp_server * helper_find_by_signature(const char * signature, const char * user)
{
	char copy[AFP_SIGNATURE_LEN];

	memcpy(copy,signature,AFP_SIGNATURE_LEN);
	return find_server_by_signature(copy,user);
}
