/* The MIT License

Copyright (c) 2010 Dylan Smith

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

TNFS daemon datagram handler

*/

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef UNIX
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include "tnfs.h"
#include "datagram.h"
#include "log.h"
#include "endian.h"
#include "session.h"
#include "errortable.h"
#include "directory.h"
#include "tnfs_file.h"

int sockfd;		/* UDP global socket file descriptor */
int tcplistenfd;	/* TCP listening socket file descriptor */

tnfs_cmdfunc dircmd[NUM_DIRCMDS]=
	{ &tnfs_opendir, &tnfs_readdir, &tnfs_closedir,
          &tnfs_mkdir, &tnfs_rmdir, &tnfs_seekdir, &tnfs_telldir };
tnfs_cmdfunc filecmd[NUM_FILECMDS]=
	{ &tnfs_open_deprecated, &tnfs_read, &tnfs_write, &tnfs_close,
	  &tnfs_stat, &tnfs_lseek, &tnfs_unlink, &tnfs_chmod, &tnfs_rename,
 		&tnfs_open	};

void tnfs_sockinit()
{
	struct sockaddr_in servaddr;

#ifdef WIN32
	WSADATA wsaData;
    	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) 
        	die("WSAStartup() failed");
#endif

	/* Create the UDP socket */
	sockfd=socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0)
		die("Unable to open socket");

	/* set up the network */
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr=htons(INADDR_ANY);
	servaddr.sin_port=htons(TNFSD_PORT);
	
	if(bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		die("Unable to bind");

	/* Create the TCP socket */
	tcplistenfd=socket(AF_INET, SOCK_STREAM, 0);
	if(tcplistenfd < 0) {
		die("Unable to create TCP socket");
	}

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family=AF_INET;
	servaddr.sin_addr.s_addr=htons(INADDR_ANY);
	servaddr.sin_port=htons(TNFSD_PORT);
	if (bind(tcplistenfd, (struct sockaddr *) &servaddr,
              sizeof(servaddr)) < 0) {
		die("Unable to bind TCP socket");
	}
	listen(tcplistenfd, 5);
}

void tnfs_mainloop()
{	
	int readyfds, i;
	fd_set fdset;
	fd_set errfdset;
	int tcpsocks[MAX_TCP_CONN];

	memset(&tcpsocks, 0, sizeof(tcpsocks));
	while(1) {
		FD_ZERO(&fdset);

		/* add UDP socket and TCP listen socket to fdset */
		FD_SET(sockfd, &fdset);
		FD_SET(tcplistenfd, &fdset);

		for(i=0; i<MAX_TCP_CONN; i++) {
			if(tcpsocks[i]) {
				FD_SET(tcpsocks[i], &fdset);
			}
		}
		
		FD_COPY(&fdset, &errfdset);
		if((readyfds=select
			(FD_SETSIZE, &fdset, NULL, &errfdset, NULL)) != 0) {
			if(readyfds < 0) {
				die("select() failed\n");
			}

			/* handle fds ready for reading */
			/* UDP message? */
			if(FD_ISSET(sockfd, &fdset)) {
				tnfs_handle_udpmsg();
			}
			/* Incoming TCP connection? */
			else if(FD_ISSET(tcplistenfd, &fdset)) {
				tcp_accept(&tcpsocks[0]);
			}

			else {
				for(i=0; i<MAX_TCP_CONN; i++) {
					if(tcpsocks[i]) {
						if(FD_ISSET(tcpsocks[i],&fdset))
						{
							tnfs_handle_tcpmsg(tcpsocks[i]);
						}
					}
				}
			}
		}
	}
}

void tcp_accept(int *socklist) {
	int acc_fd, i;
	struct sockaddr_in cli_addr;
	int cli_len=sizeof(cli_addr);
	int *fdptr;

	acc_fd=accept(tcplistenfd, (struct sockaddr *) &cli_addr, &cli_len);
	if(acc_fd < 1) {
		fprintf(stderr, "WARNING: unable to accept TCP connection\n");
		return;
	}

	fdptr=socklist;
	for(i=0; i<MAX_TCP_CONN; i++) {
		if(*fdptr == 0) {
			*fdptr=acc_fd;
			return;
		}		
	}

	/* tell the client 'too many connections' */
}

void tnfs_handle_udpmsg() {
	socklen_t len;
	int rxbytes;
	struct sockaddr_in cliaddr;
	unsigned char rxbuf[MAXMSGSZ];

	len=sizeof(cliaddr);
	rxbytes=recvfrom(sockfd, rxbuf, sizeof(rxbuf), 0,
			(struct sockaddr *)&cliaddr, &len);

	if(rxbytes >= TNFS_HEADERSZ)
	{
		/* probably a valid TNFS packet, decode it */
		tnfs_decode(&cliaddr, rxbytes, rxbuf);
	}
	else
	{
		MSGLOG(cliaddr.sin_addr.s_addr,
			"Invalid datagram received");
	}

	*(rxbuf+rxbytes)=0;
}

void tnfs_handle_tcpmsg(int cli_fd) {
	char buf[255];
	int sz;

	sz=read(cli_fd, buf, sizeof(buf));
	printf("DEBUG: rx of tcpmsg: %d bytes: %s\n", sz, buf);
	
}

void tnfs_decode(struct sockaddr_in *cliaddr, int rxbytes, unsigned char *rxbuf)
{
	Header hdr;
	Session *sess;
	int sindex;
	int datasz=rxbytes-TNFS_HEADERSZ;
	int cmdclass, cmdidx;
	unsigned char *databuf=rxbuf+TNFS_HEADERSZ;

	memset(&hdr, 0, sizeof(hdr));

	/* note: don't forget about byte alignment issues on some
	 * architectures... */
	hdr.sid=tnfs16uint(rxbuf);
	hdr.seqno=*(rxbuf+2);
	hdr.cmd=*(rxbuf+3);
	hdr.ipaddr=cliaddr->sin_addr.s_addr;
	hdr.port=ntohs(cliaddr->sin_port);

#ifdef DEBUG
	TNFSMSGLOG(&hdr, "DEBUG: Decoding datagram");
	fprintf(stderr, "DEBUG: cmd=%x msgsz=%d\n", hdr.cmd, rxbytes);
#endif

	/* The MOUNT command is the only one that doesn't need an
	 * established session (since MOUNT is actually what will
	 * establish the session) */
	if(hdr.cmd != TNFS_MOUNT)
	{
		sess=tnfs_findsession_sid(hdr.sid, &sindex);
		if(sess == NULL)
		{
			TNFSMSGLOG(&hdr, "Invalid session ID");
			return;
		}
		if(sess->ipaddr != hdr.ipaddr)
		{
			TNFSMSGLOG(&hdr, "Session and IP do not match");
			return;
		}
	}
	else
	{
		tnfs_mount(&hdr, databuf, datasz);
		return;
	}

	/* client is asking for a resend */
	if(hdr.seqno == sess->lastseqno)
	{
		tnfs_resend(sess, cliaddr);
		return;
	}

	/* find the command class and pass it off to the right
	 * function */
	cmdclass=hdr.cmd & 0xF0; 
	cmdidx=hdr.cmd & 0x0F;
	switch(cmdclass)
	{
		case CLASS_SESSION:
			switch(cmdidx)
			{
				case TNFS_UMOUNT:
				tnfs_umount(&hdr, sess, sindex);
				break;
			default:
				tnfs_badcommand(&hdr, sess);
			}
			break;
		case CLASS_DIRECTORY:
			if(cmdidx < NUM_DIRCMDS)
				(*dircmd[cmdidx])(&hdr, sess, databuf, datasz);
			else
				tnfs_badcommand(&hdr, sess);
			break;
		case CLASS_FILE:
			if(cmdidx < NUM_FILECMDS)
				(*filecmd[cmdidx])(&hdr, sess, databuf, datasz);
			else
				tnfs_badcommand(&hdr, sess);
			break;				
		default:
			tnfs_badcommand(&hdr, sess);
	}
}

void tnfs_badcommand(Header *hdr, Session *sess)
{
	TNFSMSGLOG(hdr, "Bad command");
	hdr->status=TNFS_ENOSYS;
	tnfs_send(sess, hdr, NULL, 0);
}

void tnfs_send(Session *sess, Header *hdr, unsigned char *msg, int msgsz)
{
	struct sockaddr_in cliaddr;
	ssize_t txbytes;
	unsigned char *txbuf=sess->lastmsg;

	if(msgsz+TNFS_HEADERSZ > MAXMSGSZ)
	{
		die("tnfs_send: Message too big");
	}

	cliaddr.sin_family=AF_INET;
	cliaddr.sin_addr.s_addr=hdr->ipaddr;
	cliaddr.sin_port=htons(hdr->port);

	uint16tnfs(txbuf, hdr->sid);
	*(txbuf+2)=hdr->seqno;
	*(txbuf+3)=hdr->cmd;
	*(txbuf+4)=hdr->status;
	if(msg)
		memcpy(txbuf+5, msg, msgsz);
	sess->lastmsgsz=msgsz+TNFS_HEADERSZ+1;	/* header + status code */
	sess->lastseqno=hdr->seqno;

      	txbytes=sendto(sockfd, txbuf, msgsz+TNFS_HEADERSZ+1, 0,
		(struct sockaddr *)&cliaddr, sizeof(cliaddr));
	if(txbytes < msgsz+TNFS_HEADERSZ)
	{
		TNFSMSGLOG(hdr, "Message was truncated");
	}
}

void tnfs_resend(Session *sess, struct sockaddr_in *cliaddr)
{
	int txbytes;
	txbytes=sendto(sockfd, sess->lastmsg, sess->lastmsgsz, 0,
			(struct sockaddr *)cliaddr, sizeof(struct sockaddr_in));
	if(txbytes < sess->lastmsgsz)
	{
		MSGLOG(cliaddr->sin_addr.s_addr, 
			"Retransmit was truncated");
	}
}

