#include <errno.h>
#include <string.h>

#include "afp.h"

static void quiet_log(void * priv, enum loglevels level, int type,
	const char * message)
{
	(void) priv; (void) level; (void) type; (void) message;
}

static struct libafpclient client = {
	.unmount_volume = NULL,
	.log_for_client = quiet_log,
	.forced_ending_hook = NULL,
	.scan_extra_fds = NULL,
	.loop_started = NULL,
};

void lc_start(void)
{
	libafpclient_register(&client);
	init_uams();
	afp_main_quick_startup(NULL);
}

/* Returns errno as afp_server_full_connect() left it, or 0 if it connected.
 * errno starts at a value no failure here should produce. */
int lc_login_errno(const char * host, int port)
{
	struct afp_connection_request req;

	memset(&req,0,sizeof(req));
	afp_default_url(&req.url);
	req.uam_mask=default_uams_mask();
	req.url.port=port;
	strcpy(req.url.servername,host);
	strcpy(req.url.username,"someone");
	strcpy(req.url.password,"secret");

	errno=EBADF;
	if (afp_server_full_connect(NULL,&req)!=NULL) return 0;
	return errno;
}
