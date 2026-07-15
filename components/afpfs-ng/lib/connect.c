/*
 *  connect.c
 *
 *  Copyright (C) 2007 Alex deVries
 *
 */

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "afp.h"
#include "dsi.h"
#include "dsi_protocol.h"
#include "utils.h"
#include "uams_def.h"
#include "codepage.h"
#include "users.h"
#include "libafpclient.h"
#include "server.h"



struct addrinfo * afp_get_address(void * priv, const char * hostname, unsigned int port)
{
	char port_string[6];
	struct addrinfo hints;
	struct addrinfo * addresses;

	memset(&hints, 0, sizeof(struct addrinfo));
    	hints.ai_family = PF_UNSPEC;
    	hints.ai_socktype = SOCK_STREAM;

	snprintf(port_string, sizeof(port_string), "%u", port);
	int res = getaddrinfo(hostname, port_string, &hints, &addresses);
	if (res != 0 ) {
		log_for_client(priv,AFPFSD,LOG_ERR,
		"Could not resolve %s.\n",hostname);
		return NULL;
	}

	return addresses;
}



struct afp_server * afp_server_full_connect (void * priv, struct afp_connection_request *req)
{
	int ret;
	struct addrinfo * address;
	struct afp_server  * s=NULL;
	struct afp_server  * tmpserver;
	char signature[AFP_SIGNATURE_LEN];
	unsigned char versions[SERVER_MAX_VERSIONS];
	unsigned int uams;
	char machine_type[AFP_MACHINETYPE_LEN];
	char server_name[AFP_SERVER_NAME_LEN];
        char server_name_utf8[AFP_SERVER_NAME_UTF8_LEN];
        char server_name_printable[AFP_SERVER_NAME_UTF8_LEN];
	unsigned int rx_quantum;
	char icon[AFP_SERVER_ICON_LEN];

	if ((address = afp_get_address(priv,req->url.servername, req->url.port)) == NULL)
		goto error;

	if ((s=find_server_by_address(address))) goto have_server;

	if ((tmpserver=afp_server_init(address))==NULL) goto error;

	if ((ret=afp_server_connect(tmpserver,1))<0) {
		if (ret==-ETIMEDOUT) {
			log_for_client(priv,AFPFSD,LOG_ERR,
				"Could not connect, never got a response to getstatus, %s\n",strerror(-ret));
		} else {
			log_for_client(priv,AFPFSD,LOG_ERR,
				"Could not connect, %s\n",strerror(-ret));
		}
		afp_server_remove(tmpserver);
		goto error;
	}
	/* Send DSICloseSession before disconnecting.  If we just close the
	 * TCP socket without this, macOS AFP treats the abrupt disconnect as
	 * a misbehaving client and sends an AFPATTN_SHUTDOWN attention packet
	 * to our very next connection, killing authentication before it starts.
	 * A clean close + brief pause lets macOS settle before we reconnect. */
	{
		struct dsi_header close_hdr;
		dsi_setup_header(tmpserver, &close_hdr, DSI_DSICloseSession);
		dsi_send(tmpserver, (char*)&close_hdr, sizeof(close_hdr),
		         DSI_DONT_WAIT, 0, NULL);
		usleep(100000); /* 100 ms - allow macOS to process the close */
	}
	loop_disconnect(tmpserver);

	memcpy(icon,&tmpserver->icon,AFP_SERVER_ICON_LEN);
	memcpy(&versions,&tmpserver->versions,SERVER_MAX_VERSIONS);
	uams=tmpserver->supported_uams;
	memcpy(signature,&tmpserver->signature,AFP_SIGNATURE_LEN);

	memcpy(machine_type,&tmpserver->machine_type,AFP_MACHINETYPE_LEN);
	memcpy(server_name,&tmpserver->server_name,AFP_SERVER_NAME_LEN);
	memcpy(server_name_utf8,&tmpserver->server_name_utf8,
		AFP_SERVER_NAME_UTF8_LEN);
	memcpy(server_name_printable,&tmpserver->server_name_printable,
		AFP_SERVER_NAME_UTF8_LEN);
	rx_quantum=tmpserver->rx_quantum;

	afp_server_remove(tmpserver);

	s=find_server_by_signature(signature);

	if (!s) {
		s = afp_server_init(address);

		if (afp_server_connect(s,0) !=0) {
			log_for_client(priv,AFPFSD,LOG_ERR,
				"Could not connect to server error: %s\n",
				strerror(errno));
			goto error;
		}

		//if our user and password strings are both empty and if
		//the server supports anonymous logins, pretend we only support
		//that as auth will never succeed with such credentials
		if(*req->url.username == '\0' && *req->url.password == '\0'
			&& (uams & UAM_NOUSERAUTHENT)) {
			req->uam_mask = UAM_NOUSERAUTHENT;
		}


		if ((afp_server_complete_connection(priv,
			s,address,(unsigned char *) &versions,uams,
			req->url.username, req->url.password, 
			req->url.requested_version, req->uam_mask))==NULL) {
			goto error;
		}
		s->supported_uams=uams;
		memcpy(s->signature,signature,AFP_SIGNATURE_LEN);
		memcpy(s->server_name,server_name,AFP_SERVER_NAME_LEN);
                memcpy(s->server_name_utf8,server_name_utf8,
                        AFP_SERVER_NAME_UTF8_LEN);
                memcpy(s->server_name_printable,server_name_printable,
                        AFP_SERVER_NAME_UTF8_LEN);
		memcpy(s->machine_type,machine_type,AFP_MACHINETYPE_LEN);
		memcpy(s->icon,icon,AFP_SERVER_ICON_LEN);
		s->rx_quantum=rx_quantum;
	} 
have_server:

	/* Figure out if we're using netatalk */
	if (strcmp(s->machine_type,"Netatalk")==0)
		s->server_type=AFPFS_SERVER_TYPE_NETATALK;
	else if (strcmp(s->machine_type,"Airport")==0)
		s->server_type=AFPFS_SERVER_TYPE_AIRPORT;
	else if (strcmp(s->machine_type,"Macintosh")==0)
		s->server_type=AFPFS_SERVER_TYPE_MACINTOSH;
	else 
		s->server_type=AFPFS_SERVER_TYPE_UNKNOWN;

	return s;
error:
	if ((s) && (!something_is_mounted(s))) { /* FIXME */
		afp_server_remove(s);
	}
	return NULL;
}

