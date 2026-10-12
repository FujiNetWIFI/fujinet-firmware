#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afp.h"
#include "dsi.h"

static int unmounts;

static void quiet_log(void * priv, enum loglevels level, int type,
	const char * message)
{
	(void) priv; (void) level; (void) type; (void) message;
}

static int count_unmount(struct afp_volume * volume)
{
	(void) volume;
	unmounts++;
	return 0;
}

static struct libafpclient client = {
	.unmount_volume = count_unmount,
	.log_for_client = quiet_log,
	.forced_ending_hook = NULL,
	.scan_extra_fds = NULL,
	.loop_started = NULL,
};

void cs_start_loop(void)
{
	libafpclient_register(&client);
	afp_main_quick_startup(NULL);
}

int cs_unmount_count(void)
{
	return unmounts;
}

struct afp_server * cs_connect(int port)
{
	struct addrinfo hints, * res=NULL;
	char service[8];
	struct afp_server * s;

	memset(&hints,0,sizeof(hints));
	hints.ai_family=AF_INET;
	hints.ai_socktype=SOCK_STREAM;
	snprintf(service,sizeof(service),"%d",port);
	if (getaddrinfo("127.0.0.1",service,&hints,&res)!=0) return NULL;
	s=afp_server_init(res);
	if (s==NULL) return NULL;
	if (afp_server_connect(s,0)!=0) return NULL;
	return s;
}

/* Gives a connected server the state of a completed login with one volume
 * mounted, without a real server to log in to. */
void cs_fake_mounted_login(struct afp_server * s)
{
	struct afp_volume * v=calloc(1,sizeof(*v));

	pthread_mutex_init(&v->did_cache_mutex,NULL);
	pthread_mutex_init(&v->open_forks_mutex,NULL);
	v->server=s;
	v->mounted=AFP_VOLUME_MOUNTED;
	s->volumes=v;
	s->num_volumes=1;
	s->logged_in=1;
}

int cs_open_session(struct afp_server * s)
{
	return dsi_opensession(s);
}

/* Set when the event loop has processed the server's reply to that. */
unsigned int cs_tx_quantum(struct afp_server * s)
{
	return s->tx_quantum;
}

int cs_listed(struct afp_server * s)
{
	return server_still_valid(s);
}

int cs_anything_mounted(struct afp_server * s)
{
	return something_is_mounted(s);
}

int cs_logged_in(struct afp_server * s)
{
	return s->logged_in;
}

void cs_reap(void)
{
	afp_server_reap_closed();
}
