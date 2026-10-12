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

/* What afp_server_reconnect() does to a server that is already listed. */
int helper_connect_again(struct afp_server * s)
{
	return afp_server_connect(s,0);
}

/* Number of servers in the list, or -1 if the walk does not end. */
int helper_list_length(int limit)
{
	struct afp_server * s;
	int n=0;

	for (s=get_server_base();s;s=s->next)
		if (++n>limit) return -1;
	return n;
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

void helper_start_loop(void)
{
	libafpclient_register(NULL);
	afp_main_quick_startup(NULL);
}

/* A login that fails after the session opens: the server offers no AFP
 * version this library knows.  Returns the server, still owned by the caller. */
struct afp_server * helper_failed_login(int port)
{
	unsigned char versions[SERVER_MAX_VERSIONS];
	char user[AFP_MAX_USERNAME_LEN], password[AFP_MAX_PASSWORD_LEN];
	struct afp_server * s=helper_connect(port);

	if (s==NULL) return NULL;
	memset(versions,0,sizeof(versions));
	memset(user,0,sizeof(user));
	memset(password,0,sizeof(password));
	if (afp_server_complete_connection(NULL,s,s->address,versions,0,
		user,password,0,0)!=NULL) return NULL;
	return s;
}

int helper_still_listed(struct afp_server * s)
{
	return server_still_valid(s);
}

int helper_remove(struct afp_server * s)
{
	return afp_server_remove(s);
}
