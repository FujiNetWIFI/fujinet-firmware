/* The only file in FujiNet that includes afpfs-ng's headers.  See afpBridge.h. */

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#include "afp.h"
#include "dsi.h"

#include "afpBridge.h"

static afp_bridge_log_fn log_sink;
static afp_bridge_gone_fn gone_sink;

static pthread_mutex_t start_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t loop_cond=PTHREAD_COND_INITIALIZER;
static int loop_running;
static int start_result=1;	/* 1: not tried yet */

static void on_log(void * priv, enum loglevels level, int type,
	const char * message)
{
	(void) priv; (void) level; (void) type;
	if (log_sink) log_sink(message);
}

static int on_unmount(struct afp_volume * volume)
{
	if (gone_sink) gone_sink(volume);
	return 0;
}

static void on_loop_started(void)
{
	pthread_mutex_lock(&start_lock);
	loop_running=1;
	pthread_cond_broadcast(&loop_cond);
	pthread_mutex_unlock(&start_lock);
}

static struct libafpclient client = {
	.unmount_volume = on_unmount,
	.log_for_client = on_log,
	.forced_ending_hook = NULL,
	.scan_extra_fds = NULL,
	.loop_started = on_loop_started,
};

int afp_bridge_start(afp_bridge_log_fn log, afp_bridge_gone_fn gone)
{
	struct timespec deadline;
	int ret;

	pthread_mutex_lock(&start_lock);
	if (start_result==1) {
		log_sink=log;
		gone_sink=gone;
		/* The library logs through this table from its first call on. */
		libafpclient_register(&client);
		init_uams();
		afp_main_quick_startup(NULL);

		/* The loop clears its descriptor set as it starts, so nothing
		 * may connect until it has been round once. */
		clock_gettime(CLOCK_REALTIME,&deadline);
		deadline.tv_sec+=5;
		while (!loop_running)
			if (pthread_cond_timedwait(&loop_cond,&start_lock,
				&deadline)!=0) break;
		start_result=loop_running ? 0 : -1;
	}
	ret=start_result;
	pthread_mutex_unlock(&start_lock);
	return ret;
}

struct afp_server * afp_bridge_login(const char * host, int port,
	const char * user, const char * password, int * error)
{
	struct afp_connection_request req;
	struct addrinfo * resolved=NULL;
	struct afp_server * server;

	memset(&req,0,sizeof(req));
	afp_default_url(&req.url);
	req.uam_mask=default_uams_mask();
	if (port>0) req.url.port=port;

	if ((strlen(host)>=sizeof(req.url.servername)) ||
		(strlen(user)>=sizeof(req.url.username)) ||
		(strlen(password)>=sizeof(req.url.password))) {
		*error=-ENAMETOOLONG;
		return NULL;
	}
	strcpy(req.url.servername,host);
	strcpy(req.url.username,user);
	strcpy(req.url.password,password);

	/* The library reports every failure the same way, so find out here
	 * whether the name resolves at all. */
	if (getaddrinfo(host,NULL,NULL,&resolved)!=0) {
		*error=-EHOSTUNREACH;
		return NULL;
	}
	freeaddrinfo(resolved);

	server=afp_server_full_connect(NULL,&req);
	if (server==NULL) {
		/* The library does not say why: a refused login and a server
		 * that never answers look the same from here. */
		*error=-EACCES;
		return NULL;
	}

	/* An existing login is found by server and user alone, so the
	 * password has to be checked against the one it was made with. */
	if (strcmp(server->password,password)!=0) {
		*error=-EACCES;
		return NULL;
	}
	return server;
}

int afp_bridge_volume_count(struct afp_server * server)
{
	return server->num_volumes;
}

const char * afp_bridge_volume_name(struct afp_server * server, int index)
{
	return server->volumes[index].volume_name_printable;
}

struct afp_volume * afp_bridge_find_volume(struct afp_server * server,
	const char * name)
{
	return find_volume_by_name(server,name);
}

int afp_bridge_attach(struct afp_volume * volume)
{
	char mesg[256];
	unsigned int len=0;

	mesg[0]='\0';
	if (afp_connect_volume(volume,volume->server,mesg,&len,sizeof(mesg))) {
		if (len && log_sink) log_sink(mesg);
		return -1;
	}
	return 0;
}

void afp_bridge_detach(struct afp_volume * volume)
{
	afp_unmount_volume(volume);
}

void afp_bridge_release(struct afp_server * server)
{
	if (!server_still_valid(server)) return;
	if (something_is_mounted(server)) return;
	afp_logout(server,DSI_DONT_WAIT);
	afp_server_remove(server);
}
